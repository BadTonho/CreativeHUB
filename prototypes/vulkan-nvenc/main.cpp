#include <creative_suite/composition/vulkan_native_export.h>
#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/diagnostics/logger.h>
#include <QGuiApplication>
#include <QDir>
#include <QTemporaryDir>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace c=creative_suite::composition;
namespace m=creative_suite::media;
namespace d=creative_suite::diagnostics;
using Clock=std::chrono::steady_clock;
using Status=c::GpuCompositionStatus;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class Result> void complete(const Result& result){require(result.status==Status::Complete,result.cause+" code="+std::to_string(result.error_code));}
std::filesystem::path path(const QString& value){const auto bytes=value.toUtf8();return std::filesystem::u8path(bytes.constData());}
struct ProbeUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };
struct Session {
    m::NativeVideoFramePool pool;
    std::shared_ptr<c::VulkanDevice> device;
    explicit Session(int w,int h,std::uint64_t limit=256ULL*1024*1024,bool native=true) try :pool(w,h,2){
        auto frame=pool.acquire();std::string cause;auto luid=c::VulkanNativeExportBridge::adapterLuid(frame,cause);require(bool(luid),cause);
        c::VulkanDevice::CreateOptions options;options.adapter_luid=luid;options.experimental_native_export=native;
        options.external_reserved_bytes=pool.reserved_bytes();options.known_byte_limit=limit;
        device=c::VulkanDevice::create(cause,options);require(bool(device),cause);
        std::cout<<"device="<<device->name().toStdString()<<" native_pool_reserved_estimate="<<pool.reserved_bytes()<<"\n";
    } catch(const std::exception& error){throw ProbeUnavailable(error.what());}
};
m::RgbaFrame fixture(int w,int h,bool alpha=false){
    m::RgbaFrame result{w,h,w*4,{}};result.rgba_pixels.resize(std::size_t(w)*h*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto* p=result.rgba_pixels.data()+std::size_t(y)*result.stride+x*4;
        p[0]=std::uint8_t(x*13+y*7);p[1]=std::uint8_t(x*5+y*17);p[2]=std::uint8_t(x*29+y*3);p[3]=alpha?std::uint8_t(x*31+y*11):255;}
    return result;
}
void parity(const m::RgbaFrame& a,const m::RgbaFrame& b){
    require(a.width==b.width && a.height==b.height,"Native copy changed dimensions.");
    for(int y=0;y<a.height;++y)for(int x=0;x<a.width*4;++x)
        require(std::abs(int(a.rgba_pixels[std::size_t(y)*a.stride+x])-int(b.rgba_pixels[std::size_t(y)*b.stride+x]))<=(x%4==3?0:2),"Native copy changed channels, orientation or alpha.");
}
m::VideoEncodingSettings encoding(const std::filesystem::path& output,const std::string& codec,int w,int h){
    m::VideoEncodingSettings result;result.output_path=output;result.container_name="matroska";result.video_encoder_name=codec;
    result.width=w;result.height=h;result.frame_rate_numerator=30;result.frame_rate_denominator=1;result.video_bitrate_mbps=w<=320?1:20;return result;
}
struct RunOptions {bool native=true;int warm=0,frames=12,cancel_at=-1,fail_at=-1;bool verify=false;};
struct RunMetrics {
    double total_ms=0,delivery_average_ms=0,delivery_p95_ms=0,encode_submission_ms=0;
    std::uint64_t upload_bytes=0,readback_bytes=0,oracle_readback_bytes=0,encoder_upload_bytes=0,peak_known_bytes=0;
    c::VulkanNativeExportBridge::Metrics bridge;
    unsigned active_slots=0,peak_pending=0;
    bool cancelled=false;
};
RunMetrics run(Session& session,int w,int h,const std::string& codec,const std::filesystem::path& output,const RunOptions& options){
    RunMetrics metrics;
    c::VulkanFrameCompositor compositor(session.device,128ULL*1024*1024,3);
    c::VulkanNativeExportBridge bridge(session.device);
    auto initial=session.pool.acquire();require(bool(initial),"Initial encoding frame is occupied.");
    unsigned capacity=2;
    auto settings=encoding(output.string()+".partial",codec,w,h);
    if(options.native)settings.native_frame_template=initial;
    struct StagedOutput {std::filesystem::path value;~StagedOutput(){std::error_code ignored;std::filesystem::remove(value,ignored);}} staged{settings.output_path};
    m::VideoEncoder encoder(settings);settings.native_frame_template.reset();
    require(!options.native || encoder.acceptsNativeFrames(),"The selected encoder did not accept native D3D11 frames.");
    if(options.native){complete(bridge.prepare(initial));capacity=bridge.slotCount();}
    else {
        // Equal conservative flight limits: source, UINT/normalized scratch,
        // native intermediate or readback buffer, and the native pool reservation.
        const auto bytes=std::uint64_t(w)*h*4;
        if(bytes*8+session.pool.reserved_bytes()>256ULL*1024*1024)capacity=1;
        complete(compositor.prepareAsyncReadback(w,h,64ULL*1024*1024,capacity,&capacity));
    }
    metrics.active_slots=capacity;
    initial.reset();
    auto background=fixture(512,288),overlay=fixture(256,144,true),marker=fixture(64,36);
    if(options.verify)for(auto* image:{&background,&overlay})for(int y=0;y<image->height;++y)for(int x=0;x<image->width;++x){
        auto* pixel=image->rgba_pixels.data()+std::size_t(y)*image->stride+x*4;
        pixel[0]=std::uint8_t(x/3);pixel[1]=std::uint8_t(y/2);pixel[2]=std::uint8_t((x+y)/5);
    }
    std::vector<c::CompositionLayer> layers{{&background},{&overlay},{&overlay}};
    layers[1].transform.rotation_degrees=23;layers[2].transform.position_x=.7;
    layers[2].transform.opacity=.6;const std::vector<creative_suite::effects::EffectInstance> stack{{"video.brightness",{{"amount",7}},true},{"video.saturation",{{"amount",80}},true}};layers[1].effect_stack=stack;
    layers.push_back({&marker});layers.back().transform.scale=.12;layers.back().transform.position_x=.12;layers.back().transform.position_y=.12;
    struct Item {int index=0;c::VulkanNativeExportTicket native;c::GpuReadbackTicket rgba;Clock::time_point start;std::optional<m::RgbaFrame> reference;};
    std::deque<Item> queue;std::atomic_bool cancel=false;std::vector<double> delivery;
    Clock::time_point measured_start;bool started=false;std::vector<m::RgbaFrame> encoded_references;
    c::VulkanNativeExportBridge::Metrics warmed_bridge;
    std::uint64_t warmed_encoder_uploads=0;
    const auto write=[&](Item item){
        if(options.cancel_at==item.index)cancel=true;
        if(cancel){metrics.cancelled=true;return;}
        m::NativeVideoFramePtr native;std::optional<m::RgbaFrame> rgba;
        if(options.native){auto ready=bridge.collect(item.native,[&]{return cancel.load();});complete(ready);native=std::move(ready.frame);}
        else {auto ready=compositor.collectAsyncReadback(item.rgba,[&]{return cancel.load();});complete(ready);rgba=std::move(ready.frame);}
        const auto delivered=Clock::now();
        if(item.index>=options.warm)delivery.push_back(std::chrono::duration<double,std::milli>(delivered-item.start).count());
        if(options.verify){
            require(bool(item.reference),"CPU oracle is missing.");
            if(native){parity(*item.reference,*native->download_rgba());metrics.oracle_readback_bytes+=std::uint64_t(w)*h*4;}
            else parity(*item.reference,*rgba);
            encoded_references.push_back(std::move(*item.reference));
        }
        if(options.fail_at==item.index)throw m::VideoEncodingError("Injected encoder failure.",-1);
        const auto encoding_start=Clock::now();
        if(native)encoder.writeVideo(native,item.index);else encoder.writeVideo(*rgba,item.index);
        if(item.index>=options.warm)metrics.encode_submission_ms+=std::chrono::duration<double,std::milli>(Clock::now()-encoding_start).count();
    };
    try {
        for(int index=0;index<options.warm+options.frames && !cancel;++index){
            // Warm-up is drained before the measured interval; frame retention
            // and codec configuration remain identical across both paths.
            if(index==options.warm){while(!queue.empty()){auto item=queue.front();queue.pop_front();write(item);}warmed_bridge=bridge.metrics();warmed_encoder_uploads=encoder.uploadedVideoBytes();measured_start=Clock::now();started=true;}
            if(queue.size()>=capacity){auto item=queue.front();queue.pop_front();write(item);if(cancel)break;}
            for(std::size_t offset=0;offset<marker.rgba_pixels.size();offset+=4){
                for(int channel=0;channel<3;++channel)marker.rgba_pixels[offset+channel]=channel==index%3?230:20;
                marker.rgba_pixels[offset+3]=255;
            }
            Item item;item.index=index;item.start=Clock::now();
            if(options.verify)item.reference=c::FrameCompositor::compose(w,h,layers);c::OpenGlCompositionTimings timing;
            auto batch=compositor.beginCommandBatch();
            auto source=compositor.composeTexture(w,h,layers,{},&timing);
            if(source.status==Status::Busy && !queue.empty()){
                while(!queue.empty()){auto older=queue.front();queue.pop_front();write(older);}if(cancel)break;capacity=1;
                source=compositor.composeTexture(w,h,layers,{},&timing);
            }
            require(bool(source.frame),source.cause);
            if(options.native){
                auto target=session.pool.acquire();require(bool(target),"NVENC retained all bounded input surfaces.");
                auto submitted=bridge.submit(source.frame,target);
                if(submitted.status==Status::Busy && !queue.empty()){
                    while(!queue.empty()){auto older=queue.front();queue.pop_front();write(older);}if(cancel)break;capacity=1;
                    submitted=bridge.submit(source.frame,target);
                }
                complete(submitted);item.native=submitted.ticket;
            }else {auto submitted=compositor.submitAsyncReadback(source.frame);complete(submitted);item.rgba=submitted.ticket;}
            if(index>=options.warm){metrics.upload_bytes+=timing.uploaded_bytes;if(!options.native)metrics.readback_bytes+=std::uint64_t(w)*h*4;}
            if(batch)complete(batch->finish());
            queue.push_back(item);metrics.peak_pending=std::max(metrics.peak_pending,unsigned(queue.size()));
            metrics.peak_known_bytes=std::max(metrics.peak_known_bytes,session.device->peakBytes()+session.pool.reserved_bytes());
            require(metrics.peak_known_bytes<=256ULL*1024*1024,"Combined known prototype budget exceeded 256 MiB.");
        }
        while(!queue.empty() && !cancel){auto item=queue.front();queue.pop_front();write(item);}
        bridge.discard();compositor.discardAsyncReadbacks();
        if(cancel){metrics.cancelled=true;return metrics;}
        encoder.finish();metrics.total_ms=std::chrono::duration<double,std::milli>(Clock::now()-measured_start).count();
        metrics.encoder_upload_bytes=encoder.uploadedVideoBytes()-warmed_encoder_uploads;metrics.bridge=bridge.metrics();
        metrics.bridge.submissions-=warmed_bridge.submissions;metrics.bridge.collections-=warmed_bridge.collections;
        metrics.bridge.submit_nanoseconds-=warmed_bridge.submit_nanoseconds;metrics.bridge.wait_nanoseconds-=warmed_bridge.wait_nanoseconds;metrics.bridge.vulkan_blit_bytes-=warmed_bridge.vulkan_blit_bytes;metrics.bridge.d3d_copy_bytes-=warmed_bridge.d3d_copy_bytes;
        require(m::publishEncodedFileAtomically(settings.output_path,output),"Cannot publish completed prototype output atomically.");
        if(options.verify){
            auto decoder=m::VideoPlaybackSession::open(output);int count=0;while(auto frame=decoder->decode_next_frame()){
                require((*frame)->width==w && (*frame)->height==h,"Encoded native dimensions changed.");
                require(count<int(encoded_references.size()),"Encoder duplicated an output frame.");
                double difference=0;
                for(std::size_t offset=0;offset<(*frame)->rgba_pixels.size();++offset)if(offset%4!=3)
                    difference+=std::abs(int((*frame)->rgba_pixels[offset])-int(encoded_references[count].rgba_pixels[offset]));
                require(difference/(w*h*3)<12,"Encoded output exceeded the existing mean RGB tolerance of 12.");
                const auto offset=int(h*.12)*(*frame)->stride+int(w*.12)*4;
                const auto* marker_pixel=(*frame)->rgba_pixels.data()+offset;
                require(marker_pixel[count%3]>marker_pixel[(count+1)%3]+80 && marker_pixel[count%3]>marker_pixel[(count+2)%3]+80,"Encoded frame markers were reordered.");
                ++count;}
            require(count==options.warm+options.frames,"Encoded frame count changed.");
            AVFormatContext* raw=nullptr;require(avformat_open_input(&raw,output.string().c_str(),nullptr,nullptr)>=0,"Cannot inspect encoded timestamps.");
            struct Format {AVFormatContext* value;~Format(){avformat_close_input(&value);}} format{raw};
            require(avformat_find_stream_info(raw,nullptr)>=0,"Cannot inspect encoded stream metadata.");
            int packet_index=0;AVPacket* packet=av_packet_alloc();require(packet,"Cannot allocate timestamp packet.");
            struct Packet {AVPacket* value;~Packet(){av_packet_free(&value);}} retained_packet{packet};
            while(av_read_frame(raw,packet)>=0){
                if(raw->streams[packet->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
                    require(packet->pts!=AV_NOPTS_VALUE && av_rescale_q(packet->pts,raw->streams[packet->stream_index]->time_base,AVRational{1,30})==packet_index,
                        "Encoded timestamps changed order or timeline position.");++packet_index;
                }
                av_packet_unref(packet);
            }
            require(packet_index==count,"Encoded packet count did not match frame timestamps.");
        }
        require(started && delivery.size()==std::size_t(options.frames),"Measured frames were lost or duplicated.");
        metrics.delivery_average_ms=std::accumulate(delivery.begin(),delivery.end(),0.0)/delivery.size();
        std::sort(delivery.begin(),delivery.end());metrics.delivery_p95_ms=delivery[std::size_t(std::ceil(delivery.size()*.95))-1];
        if(options.native)require(metrics.readback_bytes==0 && metrics.encoder_upload_bytes==0,"Native mode transferred output pixels through the CPU.");
        return metrics;
    }catch(...){bridge.discard();compositor.discardAsyncReadbacks();throw;}
}
void selfTest(Session& session,const std::filesystem::path& root){
    c::VulkanFrameCompositor compositor(session.device);c::VulkanNativeExportBridge bridge(session.device);
    auto a=fixture(320,180,true);c::GpuImageOperation operation;operation.background.rgba=&a;
    auto source=compositor.processImage(operation);require(bool(source.frame),source.cause);
    auto first=session.pool.acquire(),second=session.pool.acquire();require(first && second,"Pool did not provide two native frames.");
    require(bridge.prepare(first,2,true).status==Status::Unsupported,"Unsupported format was not rejected.");
    complete(bridge.prepare(first));require(bridge.slotCount()==2,"Small prototype did not prepare two slots.");
    auto wrong_pixels=fixture(32,24);c::GpuImageOperation wrong_operation;wrong_operation.background.rgba=&wrong_pixels;
    auto wrong_source=compositor.processImage(wrong_operation);require(bool(wrong_source.frame),wrong_source.cause);
    require(bridge.submit(wrong_source.frame,first).status==Status::Unsupported,"Mismatched source geometry was accepted.");
    auto one=bridge.submit(source.frame,first),two=bridge.submit(source.frame,second);complete(one);complete(two);
    require(bridge.submit(source.frame,first).status==Status::Busy,"Native flights exceeded their limit.");
    require(bridge.collect(two.ticket).status==Status::Busy,"Native ticket collection escaped FIFO.");
    require(bridge.collect({9999999}).status==Status::Unsupported,"Invalid native ticket was accepted.");
    require(bridge.collect(one.ticket,[]{return true;}).status==Status::Cancelled,"Pending native collection ignored cancellation.");
    auto collected=bridge.collect(one.ticket);complete(collected);parity(a,*collected.frame->download_rgba());
    auto last=bridge.collect(two.ticket);complete(last);parity(a,*last.frame->download_rgba());
    require(bridge.collect(one.ticket).status==Status::Unsupported,"Retired ticket was reused.");
    collected.frame.reset();last.frame.reset();first.reset();second.reset();
    auto target=session.pool.acquire();auto reuse=bridge.submit(source.frame,target);complete(reuse);bridge.discard();
    require(bridge.collect(reuse.ticket).status==Status::Unsupported,"Discarded ticket remained valid.");
    require(bridge.submit(source.frame,target,[]{return true;}).status==Status::Cancelled,"Submission ignored cancellation.");
    complete(bridge.prepare(target,1));require(bridge.slotCount()==1,"Explicit single-flight limit was ignored.");
    auto pending=bridge.submit(source.frame,target);complete(pending);bridge.discard();target.reset();
    {c::VulkanNativeExportBridge retiring(session.device);auto destination=session.pool.acquire();complete(retiring.prepare(destination));complete(retiring.submit(source.frame,destination));}
    for(const auto& codec:{std::string("h264_nvenc"),std::string("hevc_nvenc")}){
        RunOptions options;options.verify=true;run(session,320,180,codec,root/(codec+".mkv"),options);
    }
    const auto destination=root/"preserve.mkv";
    for(const bool fail:{false,true}){
        {std::ofstream old(destination,std::ios::binary);old<<"preserve-existing";}
        RunOptions options;options.cancel_at=fail?-1:2;options.fail_at=fail?2:-1;
        bool failed=false;try{auto result=run(session,320,180,"h264_nvenc",destination,options);require(result.cancelled,"Cancellation did not stop publication.");}
        catch(const m::VideoEncodingError&){failed=true;}
        require(failed==fail,"Selected encoder failure was swallowed.");
        std::ifstream input(destination,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(input),{}};
        require(bytes=="preserve-existing","Cancellation/failure replaced the existing destination.");
    }
    std::cout<<"native_export_self_test=passed codecs=h264_nvenc,hevc_nvenc parity_readbacks=explicit_unmeasured\n";
}
}
int main(int argc,char** argv){
    QGuiApplication application(argc,argv);const auto args=application.arguments();
    const bool required=args.contains("--require-native");
    QString mode="native",codec="h264_nvenc",directory="build/vulkan-nvenc-evidence";
    for(const auto& arg:args){if(arg.startsWith("--mode="))mode=arg.mid(7);if(arg.startsWith("--codec="))codec=arg.mid(8);if(arg.startsWith("--output-dir="))directory=arg.mid(13);}
    if(mode!="native" && mode!="rgba" && mode!="compare"){std::cerr<<"Use --mode=native|rgba|compare.\n";return 1;}
    const auto root=path(QDir(directory).absolutePath());std::filesystem::create_directories(root);
    if(!d::Logger::instance().initialize(root/"logs"))return 1;
    try {
        if(args.contains("--self-test")){
            QTemporaryDir fixtures(QDir(directory).absoluteFilePath("self-test-XXXXXX"));require(fixtures.isValid(),"Cannot create isolated native fixtures.");
            Session session(320,180);std::exception_ptr failure;
            std::thread worker([&]{try{selfTest(session,path(fixtures.path()));}catch(...){failure=std::current_exception();}});worker.join();if(failure)std::rethrow_exception(failure);
            // Creation options and capability rejection run on the GUI thread.
            std::string cause;auto ordinary=c::VulkanDevice::create(cause);require(bool(ordinary),cause);
            auto frame=session.pool.acquire();c::VulkanNativeExportBridge unavailable(ordinary);
            require(unavailable.prepare(frame).status==Status::Unsupported,"Ordinary Vulkan creation enabled native export implicitly.");
            c::VulkanDevice::CreateOptions wrong;wrong.adapter_luid=std::array<std::uint8_t,8>{255,255,255,255,255,255,255,255};wrong.experimental_native_export=true;
            require(!c::VulkanDevice::create(cause,wrong),"An unmatched adapter LUID was accepted.");
            c::VulkanDevice::CreateOptions invalid_budget;invalid_budget.known_byte_limit=0;
            require(!c::VulkanDevice::create(cause,invalid_budget),"An empty known memory budget was accepted.");
            invalid_budget.known_byte_limit=256ULL*1024*1024+1;
            require(!c::VulkanDevice::create(cause,invalid_budget),"A budget above 256 MiB was accepted.");
            invalid_budget.known_byte_limit=1024;invalid_budget.external_reserved_bytes=1024;
            require(!c::VulkanDevice::create(cause,invalid_budget),"An exhausted external reservation was accepted.");
            Session bounded(320,180,320ULL*180*4*7);
            c::VulkanNativeExportBridge single(bounded.device);auto limited=bounded.pool.acquire();complete(single.prepare(limited));
            require(single.slotCount()==1,"Memory pressure did not reduce native flights to one.");
            return 0;
        }
        int cancel_after=-1;
        for(const auto& arg:args)if(arg.startsWith("--cancel-after=")){bool valid=false;cancel_after=arg.mid(15).toInt(&valid);require(valid && cancel_after>=0,"Invalid --cancel-after frame index.");}
        bool stop=false;
        const bool benchmark=args.contains("--benchmark");
        const std::vector<std::pair<int,int>> sizes=benchmark?std::vector<std::pair<int,int>>{{1920,1080},{2560,1440},{3840,2160}}:std::vector<std::pair<int,int>>{{320,180}};
        for(const auto [w,h]:sizes){
            Session session(w,h,256ULL*1024*1024,mode!="rgba");std::exception_ptr failure;
            std::thread worker([&]{try{
                for(int trial=1;trial<=(benchmark?3:1);++trial)for(const bool native:{false,true}){
                    if((mode=="native" && !native)||(mode=="rgba" && native))continue;
                    RunOptions options;options.native=native;options.warm=benchmark?30:0;options.frames=benchmark?180:12;options.verify=!benchmark;options.cancel_at=cancel_after;
                    const auto result=run(session,w,h,codec.toStdString(),root/(std::string(native?"native-":"rgba-")+std::to_string(w)+"-"+std::to_string(trial)+".mkv"),options);
                    if(result.cancelled){std::cout<<"cancelled=1 mode="<<(native?"native":"rgba")<<"\n";stop=true;return;}
                    std::cout<<"benchmark mode="<<(native?"native":"rgba")<<" codec="<<codec.toStdString()<<" width="<<w<<" height="<<h<<" trial="<<trial
                        <<" total_ms="<<result.total_ms<<" fps="<<options.frames*1000.0/result.total_ms<<" delivery_average_ms="<<result.delivery_average_ms<<" delivery_p95_ms="<<result.delivery_p95_ms
                        <<" encode_submission_ms="<<result.encode_submission_ms<<" source_upload_bytes="<<result.upload_bytes<<" cpu_encoder_input_bytes="<<(native?0:result.readback_bytes)<<" output_readback_bytes="<<result.readback_bytes<<" oracle_readback_bytes="<<result.oracle_readback_bytes<<" encoder_explicit_upload_bytes="<<result.encoder_upload_bytes
                        <<" submissions="<<result.bridge.submissions<<" collections="<<result.bridge.collections<<" vulkan_submit_ms="<<result.bridge.submit_nanoseconds/1e6
                        <<" vulkan_blit_bytes="<<result.bridge.vulkan_blit_bytes<<" d3d_copy_bytes="<<result.bridge.d3d_copy_bytes<<" bridge_wait_ms="<<result.bridge.wait_nanoseconds/1e6
                        <<" active_slots="<<result.active_slots<<" peak_pending="<<result.peak_pending<<" known_peak_bytes="<<result.peak_known_bytes<<"\n"<<std::flush;
                }
            }catch(...){failure=std::current_exception();}});worker.join();if(failure)std::rethrow_exception(failure);if(stop)break;
        }
        return 0;
    }catch(const ProbeUnavailable& error){d::Logger::instance().log(d::Level::Error,"vulkan-nvenc-prototype","initialize",error.what());std::cerr<<error.what()<<"\n";return required?1:77;}
    catch(const std::exception& error){d::Logger::instance().log(d::Level::Error,"vulkan-nvenc-prototype","run",error.what());std::cerr<<error.what()<<"\n";return 1;}
}
