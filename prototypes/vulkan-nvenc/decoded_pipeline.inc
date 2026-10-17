// Standalone research only. No application decoder/import selection changes.
namespace {
using DecodeSession=m::VideoPlaybackSession;
struct DecodedUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
template<class Result> void pipelineComplete(const Result& result){
    if(result.status==Status::Busy || result.status==Status::Unsupported)throw DecodedUnavailable(result.cause.empty()?"Bounded pipeline capacity is occupied.":result.cause);
    complete(result);
}
struct DecodedMetrics {
    double total_ms=0,input_mean_ms=0,input_p95_ms=0,input_completion_mean_ms=0,input_completion_p95_ms=0,output_mean_ms=0,output_p95_ms=0;
    std::uint64_t source_upload=0,output_readback=0,encoder_upload=0,known_peak=0;
    m::DecodeAccelerationDiagnostics decode;
    c::VulkanNativeImportBridge::Metrics input;
    c::VulkanNativeExportBridge::Metrics output;
    unsigned active_slots=0,peak_pending=0;
    bool cancelled=false;
};
std::string decodedUtf8(const std::filesystem::path& file){const auto value=file.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
void admitDecodedSource(const std::filesystem::path& source){
    AVFormatContext* raw=nullptr;require(avformat_open_input(&raw,decodedUtf8(source).c_str(),nullptr,nullptr)>=0,"Cannot inspect prototype source.");
    struct Format {AVFormatContext* value;~Format(){avformat_close_input(&value);}} format{raw};
    require(avformat_find_stream_info(raw,nullptr)>=0,"Cannot inspect prototype source metadata.");
    const auto index=av_find_best_stream(raw,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);require(index>=0,"Prototype source has no video.");
    const auto* parameters=raw->streams[index]->codecpar;
    if(parameters->color_trc==AVCOL_TRC_SMPTE2084 || parameters->color_trc==AVCOL_TRC_ARIB_STD_B67)throw DecodedUnavailable("HDR transfer functions are outside this SDR prototype.");
}
std::unique_ptr<DecodeSession> hardwareDecoder(const std::filesystem::path& source){
    return DecodeSession::open(source,m::DecodeOptions{m::DecodeAcceleration::PreferHardware});
}
void checkDecodedIdentity(Session& session,const m::NativeVideoFramePtr& frame){
    if(!frame)throw DecodedUnavailable("The decoded pipeline requires an actual NV12 D3D11VA frame, not software/oriented recovery.");
    std::string cause;auto decoder=c::VulkanNativeImportBridge::adapterLuid(frame,cause);
    auto target=session.pool.acquire();auto encoder=c::VulkanNativeExportBridge::adapterLuid(target,cause);
    if(!decoder || !encoder || *decoder!=*encoder)throw DecodedUnavailable("Decoder and encoder adapters do not match: "+cause);
}
DecodedMetrics runDecoded(Session& session,const std::filesystem::path& source,const std::filesystem::path& destination,
    const std::string& codec,bool native_input,bool native_output,int warm,int frames,bool verify,int cancel_at=-1,int fail_at=-1,unsigned maximum=2){
    auto decoder=hardwareDecoder(source);auto probe=decoder->decode_frame_at_native(0);
    checkDecodedIdentity(session,probe?probe->native:nullptr);probe.reset();
    DecodedMetrics metrics;
    auto geometry=session.pool.acquire();require(bool(geometry),"Output geometry lease is occupied.");
    const int w=geometry->width(),h=geometry->height();geometry.reset();
    c::VulkanNativeImportBridge input(session.device);c::VulkanNativeExportBridge output(session.device);
    c::VulkanFrameCompositor compositor(session.device,192ULL*1024*1024,8);
    auto first=decoder->decode_frame_at_native(0);require(first && first->native,"D3D11VA input became unavailable.");
    unsigned capacity=w>=3840?1:std::clamp(maximum,1u,2u);
    if(native_input){pipelineComplete(input.prepare(first->native,capacity));capacity=std::min(capacity,input.slotCount());}
    auto initial=session.pool.acquire();
    if(native_output){pipelineComplete(output.prepare(initial,capacity));capacity=std::min(capacity,output.slotCount());}
    else pipelineComplete(compositor.prepareAsyncReadback(w,h,64ULL*1024*1024,capacity,&capacity));
    first.reset();
    auto temporary_path=destination;temporary_path+=".partial";auto settings=encoding(temporary_path,codec,w,h);
    if(native_output)settings.native_frame_template=initial;
    struct Temporary {std::filesystem::path path;~Temporary(){std::error_code ignored;std::filesystem::remove(path,ignored);}} temporary{settings.output_path};
    m::VideoEncoder encoder(settings);settings.native_frame_template.reset();initial.reset();
    auto oracle=verify?DecodeSession::open(source):nullptr;
    auto overlay=fixture(64,36,true);
    const std::vector<creative_suite::effects::EffectInstance> effects{{"video.brightness",{{"amount",7}},true},{"video.saturation",{{"amount",80}},true}};
    struct Item {int index;Clock::time_point start;c::VulkanNativeImportTicket input;c::VulkanNativeExportTicket output;c::GpuReadbackTicket rgba;std::optional<m::RgbaFrame> reference;double input_complete_ms=-1;};
    std::deque<Item> pending;std::vector<double> input_samples,input_completion_samples,output_samples;std::vector<m::RgbaFrame> references;
    std::atomic_bool cancel=false;Clock::time_point measured_start;
    auto encode=[&](Item item){
        if(item.index==cancel_at){cancel=true;return;}
        if(item.input.value){pipelineComplete(input.collect(item.input,[&]{return cancel.load();}));item.input_complete_ms=std::chrono::duration<double,std::milli>(Clock::now()-item.start).count();}
        if(item.index>=warm)input_completion_samples.push_back(item.input_complete_ms);
        m::NativeVideoFramePtr native;std::optional<m::RgbaFrame> rgba;
        if(native_output){auto ready=output.collect(item.output,[&]{return cancel.load();});pipelineComplete(ready);native=std::move(ready.frame);}
        else {auto ready=compositor.collectAsyncReadback(item.rgba,[&]{return cancel.load();});pipelineComplete(ready);rgba=std::move(ready.frame);}
        if(item.index>=warm)output_samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-item.start).count());
        if(verify){auto actual=native?native->download_rgba():std::make_shared<m::RgbaFrame>(*rgba);parity(*item.reference,*actual);references.push_back(std::move(*item.reference));}
        if(item.index==fail_at)throw m::VideoEncodingError("Injected decoded-pipeline encoder failure.",-1);
        if(native)encoder.writeVideo(native,item.index);else encoder.writeVideo(*rgba,item.index);
    };
    try {
        for(int index=0;index<warm+frames && !cancel;++index){
            if(index==warm){while(!pending.empty()){auto item=std::move(pending.front());pending.pop_front();encode(std::move(item));}measured_start=Clock::now();}
            if(pending.size()>=capacity){auto item=std::move(pending.front());pending.pop_front();encode(std::move(item));if(cancel)break;}
            Item item{};item.index=index;item.start=Clock::now();
            c::GpuTextureFramePtr imported;m::RgbaFramePtr rgba;
            if(native_input){auto decoded=decoder->decode_frame_at_native(index);require(decoded && decoded->native,"Native decoder failed or silently recovered through CPU.");
                auto submitted=input.submit(decoded->native,[&]{return cancel.load();});
                if(submitted.status==Status::Busy && !pending.empty()){while(!pending.empty()){auto older=std::move(pending.front());pending.pop_front();encode(std::move(older));}capacity=1;submitted=input.submit(decoded->native);}
                pipelineComplete(submitted);imported=submitted.frame;item.input=submitted.ticket;
                if(capacity==1){pipelineComplete(input.collect(item.input));item.input={};item.input_complete_ms=std::chrono::duration<double,std::milli>(Clock::now()-item.start).count();}
            }else {auto decoded=decoder->decode_frame_at(index);require(bool(decoded),"RGBA decode did not produce the requested frame.");rgba=*decoded;item.input_complete_ms=std::chrono::duration<double,std::milli>(Clock::now()-item.start).count();}
            if(index>=warm)input_samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-item.start).count());
            m::RgbaFrame placeholder{w,h,w*4,{}};
            c::CompositionLayer video;video.frame=rgba?rgba.get():&placeholder;video.texture_frame=imported;video.effect_stack=effects;
            c::CompositionLayer top;top.frame=&overlay;top.transform.scale=.3;top.transform.rotation_degrees=23;top.transform.opacity=.6;top.transform.position_x=.7;
            std::vector<c::CompositionLayer> layers{video,top};
            if(verify){auto cpu=oracle->decode_frame_at(index);require(bool(cpu),"CPU oracle frame is unavailable.");auto oracle_layers=layers;oracle_layers[0].texture_frame.reset();oracle_layers[0].frame=cpu->get();item.reference=c::FrameCompositor::compose(w,h,oracle_layers);}
            auto batch=compositor.beginCommandBatch();
            c::OpenGlCompositionTimings timing;auto composed=compositor.composeTexture(w,h,layers,{},&timing);
            if(composed.status==Status::Busy && !pending.empty()){while(!pending.empty()){auto older=std::move(pending.front());pending.pop_front();encode(std::move(older));}capacity=1;composed=compositor.composeTexture(w,h,layers,{},&timing);}
            pipelineComplete(composed);metrics.source_upload+=timing.uploaded_bytes;
            if(native_output){auto target=session.pool.acquire();require(bool(target),"NVENC retained every bounded destination.");auto submitted=output.submit(composed.frame,target);pipelineComplete(submitted);item.output=submitted.ticket;}
            else {auto submitted=compositor.submitAsyncReadback(composed.frame);pipelineComplete(submitted);item.rgba=submitted.ticket;metrics.output_readback+=std::uint64_t(w)*h*4;}
            if(batch)pipelineComplete(batch->finish());
            pending.push_back(std::move(item));metrics.peak_pending=std::max(metrics.peak_pending,unsigned(pending.size()));
            metrics.known_peak=std::max(metrics.known_peak,session.device->totalPeakBytes());require(metrics.known_peak<=256ULL*1024*1024,"Known pipeline reservation exceeded 256 MiB.");
        }
        while(!pending.empty() && !cancel){auto item=std::move(pending.front());pending.pop_front();encode(std::move(item));}
        output.discard();input.discard();compositor.discardAsyncReadbacks();require(output.isRetired() && input.isRetired(),"Pipeline retirement was not confirmed.");
        if(cancel){metrics.cancelled=true;return metrics;}
        encoder.finish();require(m::publishEncodedFileAtomically(settings.output_path,destination),"Cannot publish decoded-pipeline output.");
        metrics.total_ms=std::chrono::duration<double,std::milli>(Clock::now()-measured_start).count();
        metrics.decode=decoder->acceleration_diagnostics();metrics.input=input.metrics();metrics.output=output.metrics();metrics.encoder_upload=encoder.uploadedVideoBytes();metrics.active_slots=capacity;
        require(metrics.decode.backend==m::DecodeBackend::D3D11 && metrics.decode.software_frames==0,"Benchmark decoder escaped hardware decoding.");
        if(native_input)require(metrics.decode.downloaded_frames==0 && metrics.input.submissions==std::uint64_t(warm+frames),"Native input downloaded frames or omitted submissions.");
        if(native_output)require(metrics.output_readback==0 && metrics.encoder_upload==0,"Native output transferred pixels through CPU.");
        if(verify){auto result=DecodeSession::open(destination);int count=0;
            while(auto frame=result->decode_next_frame()){require(count<int(references.size()),"Encoder duplicated a decoded-pipeline frame.");
                require((*frame)->width==w && (*frame)->height==h,"Decoded-pipeline dimensions changed.");double difference=0;
                for(std::size_t i=0;i<(*frame)->rgba_pixels.size();++i)if(i%4!=3)difference+=std::abs(int((*frame)->rgba_pixels[i])-int(references[count].rgba_pixels[i]));
                require(difference/(w*h*3)<12,"Decoded-pipeline lossy output exceeded existing tolerance.");++count;}
            require(count==warm+frames,"Decoded pipeline lost output frames.");
            AVFormatContext* raw=nullptr;require(avformat_open_input(&raw,decodedUtf8(destination).c_str(),nullptr,nullptr)>=0,"Cannot inspect pipeline timestamps.");
            struct Format {AVFormatContext* p;~Format(){avformat_close_input(&p);}} format{raw};require(avformat_find_stream_info(raw,nullptr)>=0,"Cannot inspect pipeline stream.");
            AVPacket* packet=av_packet_alloc();require(packet,"Cannot allocate timestamp packet.");struct Packet {AVPacket* p;~Packet(){av_packet_free(&p);}} holder{packet};int index=0;
            while(av_read_frame(raw,packet)>=0){if(raw->streams[packet->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){require(av_rescale_q(packet->pts,raw->streams[packet->stream_index]->time_base,AVRational{1,30})==index,"Pipeline timestamps are out of order.");++index;}av_packet_unref(packet);}require(index==count,"Timestamp count changed.");}
        const auto summarize=[](std::vector<double>& samples,double& mean,double& p95){require(!samples.empty(),"Missing pipeline samples.");mean=std::accumulate(samples.begin(),samples.end(),0.0)/samples.size();std::sort(samples.begin(),samples.end());p95=samples[std::size_t(std::ceil(samples.size()*.95))-1];};
        require(input_samples.size()==std::size_t(frames) && output_samples.size()==std::size_t(frames),"Measured pipeline frames were lost.");
        summarize(input_samples,metrics.input_mean_ms,metrics.input_p95_ms);summarize(input_completion_samples,metrics.input_completion_mean_ms,metrics.input_completion_p95_ms);summarize(output_samples,metrics.output_mean_ms,metrics.output_p95_ms);return metrics;
    }catch(...){output.discard();input.discard();compositor.discardAsyncReadbacks();throw;}
}
void decodedBridgeTests(Session& session,const std::filesystem::path& source){
    auto decoder=hardwareDecoder(source);auto first=decoder->decode_frame_at_native(0);require(first && first->native,"Native decoder unavailable.");checkDecodedIdentity(session,first->native);
    c::VulkanNativeImportBridge bridge(session.device);require(bridge.prepare(first->native,2,true).status==Status::Unsupported,"Rejected input format was accepted.");
    complete(bridge.prepare(first->native));require(bridge.slotCount()==2,"Input bridge did not prepare two active_slots.");
    require(bridge.submit(first->native,[]{return true;}).status==Status::Cancelled,"Input cancellation was ignored.");
    auto a=bridge.submit(first->native);complete(a);auto second=decoder->decode_frame_at_native(1);auto b=bridge.submit(second->native);complete(b);
    require(bridge.submit(first->native).status==Status::Busy,"Input bridge exceeded its bounded active_slots.");
    require(bridge.collect(b.ticket).status==Status::Busy && bridge.collect({999999}).status==Status::Unsupported,"Input ticket/FIFO validation failed.");
    require(bridge.collect(a.ticket,[]{return true;}).status==Status::Cancelled,"Collect cancellation was ignored.");
    auto cpu=first->native->download_rgba();first.reset();second.reset();decoder.reset();
    c::VulkanFrameCompositor compositor(session.device);auto ready=bridge.collect(a.ticket);complete(ready);auto actual=compositor.readback(ready.frame);complete(actual);parity(*cpu,*actual.frame);
    complete(bridge.collect(b.ticket));require(bridge.collect(a.ticket).status==Status::Unsupported,"Retired input ticket was accepted.");
    auto reopened=hardwareDecoder(source);auto fresh=reopened->decode_frame_at_native(0);
    // A new D3D device must be re-prepared rather than treated as a cached slot.
    require(bridge.submit(fresh->native).status==Status::Unsupported,"A new decoder device bypassed bridge preparation.");
    ready.frame.reset();actual.frame.reset();b.frame.reset();
    fresh=reopened->decode_frame_at_native(2);
    complete(bridge.prepare(fresh->native,1));auto reuse=bridge.submit(fresh->native);complete(reuse);bridge.discard();require(bridge.isRetired(),"Pending input shutdown did not retire.");
    auto retained=compositor.readback(a.frame);complete(retained);parity(*cpu,*retained.frame);a.frame.reset();
    require(bridge.collect(reuse.ticket).status==Status::Unsupported,"Discarded input ticket was accepted.");
    for(unsigned submit_fault:{1u,2u,3u,0u}){
        complete(bridge.prepare(fresh->native,1));
        auto submitted=bridge.submit(fresh->native,{},submit_fault);
        if(submit_fault)require(submitted.status==Status::Failed,"Input submission fault was ignored.");
        else {complete(submitted);require(bridge.collect(submitted.ticket,{},true).status==Status::Failed,"Input collection fault was ignored.");}
        bridge.discard();require(bridge.isRetired(),"Input failure did not retire issued consumers safely.");
    }
}
int decodedMain(const QStringList& args,const std::filesystem::path& root,const QString& mode,const QString& output_codec){
    const bool test=args.contains("--decode-self-test"),benchmark=args.contains("--decode-benchmark");
#ifndef _WIN32
    throw DecodedUnavailable("The decoded prototype requires Windows D3D11VA and NVENC.");
#endif
    {Session capability(320,180);if(!capability.device->supportsNativeExport())throw DecodedUnavailable("The decoded prototype requires NVIDIA Win32 external-memory/keyed-mutex support.");}
    QString supplied,input_mode="native";int cancel_at=-1;
    for(const auto& arg:args){if(arg.startsWith("--decode-source="))supplied=arg.mid(16);if(arg.startsWith("--input="))input_mode=arg.mid(8);if(arg.startsWith("--cancel-after=")){bool valid=false;cancel_at=arg.mid(15).toInt(&valid);require(valid && cancel_at>=0,"Invalid --cancel-after frame index.");}}
    require(input_mode=="native" || input_mode=="rgba","Use --input=native|rgba.");
    const auto sizes=benchmark?std::vector<std::pair<int,int>>{{1920,1080},{2560,1440},{3840,2160}}:std::vector<std::pair<int,int>>{{320,180}};
    qputenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY","1");
    for(const auto [w,h]:sizes){
        const auto source=supplied.isEmpty()?root/("decoded-source-"+std::to_string(w)+".mkv"):path(supplied);
        if(supplied.isEmpty())m::test_support::createTaggedVideo(source,AVCOL_SPC_BT709,AVCOL_RANGE_MPEG,"h264_nvenc",w,h,benchmark?210:12,true);
        admitDecodedSource(source);
        auto probe_decoder=hardwareDecoder(source);auto probe=probe_decoder->decode_frame_at_native(0);
        if(!probe || !probe->native)throw DecodedUnavailable("The selected source did not produce native D3D11VA frames.");
        const int width=probe->width(),height=probe->height();
        Session session(width,height,256ULL*1024*1024,true,width>=3840?1:2);checkDecodedIdentity(session,probe->native);
        std::string identity_cause;auto luid=c::VulkanNativeImportBridge::adapterLuid(probe->native,identity_cause);
        std::cout<<"decoded_adapter_luid=";if(luid)for(auto byte:*luid)std::cout<<"0123456789abcdef"[byte>>4]<<"0123456789abcdef"[byte&15];
        std::cout<<" input_format=NV12 shared_format=BGRA8 compositor_format=RGBA8_UINT decoder_pool_accounting=separate\n";
        probe.reset();probe_decoder.reset();
        std::unique_ptr<Session> bounded;std::shared_ptr<c::VulkanDevice> ordinary;
        if(test){bounded=std::make_unique<Session>(320,180,1536ULL*1024,true,1);std::string cause;ordinary=c::VulkanDevice::create(cause);require(bool(ordinary),cause);}
        std::exception_ptr failure;std::thread worker([&]{try{
            if(test){decodedBridgeTests(session,source);
                {auto d=hardwareDecoder(source);auto f=d->decode_frame_at_native(0);
                    c::VulkanNativeImportBridge unavailable(ordinary);require(unavailable.prepare(f->native).status==Status::Unsupported,"Ordinary creation enabled input interop.");
                    c::VulkanNativeImportBridge format(session.device);auto bgra=session.pool.acquire();require(format.prepare(bgra).status==Status::Unsupported,"BGRA output was accepted as decoded NV12.");
                    c::VulkanNativeImportBridge limited(bounded->device);
                    auto occupied=bounded->device->tryReserveExternal(1024ULL*1024);require(bool(occupied),"Cannot reserve pressure fixture.");
                    require(limited.prepare(f->native).status==Status::Busy,"Input saturation did not report nontechnical Busy.");occupied.reset();
                    complete(limited.prepare(f->native));require(limited.slotCount()==1,"Input pressure did not reduce to one flight.");auto r=limited.submit(f->native);complete(r);complete(limited.collect(r.ticket));}

                for(const auto* in_codec:{"h264_nvenc","hevc_nvenc"})for(const auto matrix:{AVCOL_SPC_SMPTE170M,AVCOL_SPC_BT709,AVCOL_SPC_BT2020_NCL})for(const auto range:{AVCOL_RANGE_MPEG,AVCOL_RANGE_JPEG}){
                    const auto tagged=root/(std::string("tagged-")+in_codec+"-"+std::to_string(matrix)+"-"+std::to_string(range)+".mkv");m::test_support::createTaggedVideo(tagged,matrix,range,in_codec);
                    auto d=hardwareDecoder(tagged);auto cpu=DecodeSession::open(tagged);auto native=d->decode_frame_at_native(0);require(native && native->native,"Tagged input is not hardware decoded.");c::VulkanNativeImportBridge bridge(session.device);complete(bridge.prepare(native->native));c::VulkanFrameCompositor compositor(session.device);
                    for(int index:{0,2,1}){auto decoded=d->decode_frame_at_native(index);auto submitted=bridge.submit(decoded->native);if(submitted.status==Status::Unsupported){complete(bridge.prepare(decoded->native));submitted=bridge.submit(decoded->native);}complete(submitted);auto collected=bridge.collect(submitted.ticket);complete(collected);auto actual=compositor.readback(collected.frame);complete(actual);auto expected=cpu->decode_frame_at(index);parity(**expected,*actual.frame);m::test_support::checkTaggedPixels(*actual.frame,matrix,range,5);}
                }
                for(const auto* codec:{"h264_nvenc","hevc_nvenc"})for(const auto pair:{std::pair{true,true},std::pair{false,true},std::pair{true,false},std::pair{false,false}})
                    runDecoded(session,source,root/(std::string("pipeline-")+codec+std::to_string(pair.first)+std::to_string(pair.second)+".mkv"),codec,pair.first,pair.second,0,12,true);
                const auto hevc_source=root/"decoded-source-hevc.mkv";m::test_support::createTaggedVideo(hevc_source,AVCOL_SPC_BT709,AVCOL_RANGE_MPEG,"hevc_nvenc",320,180,12,true);
                for(const auto* codec:{"h264_nvenc","hevc_nvenc"})runDecoded(session,hevc_source,root/(std::string("hevc-input-")+codec+".mkv"),codec,true,true,0,12,true);
                const auto padded_source=root/"decoded-source-padded.mkv";m::test_support::createTaggedVideo(padded_source,AVCOL_SPC_BT709,AVCOL_RANGE_MPEG,"h264_nvenc",322,182,3);
                {auto d=hardwareDecoder(padded_source);auto f=d->decode_frame_at_native(2);require(f && f->native,"Padded source is not native.");c::VulkanNativeImportBridge bridge(session.device);complete(bridge.prepare(f->native));auto r=bridge.submit(f->native);complete(r);complete(bridge.collect(r.ticket));c::VulkanFrameCompositor compositor(session.device);auto actual=compositor.readback(r.frame);complete(actual);parity(**d->decode_frame_at(2),*actual.frame);}
                for(bool fail:{false,true}){const auto destination=root/"decoded-preserve.mkv";{std::ofstream old(destination);old<<"preserve-existing";}
                    bool failed=false;try{auto r=runDecoded(session,source,destination,"h264_nvenc",true,true,0,12,false,fail?-1:2,fail?2:-1);require(r.cancelled,"Pipeline did not cancel.");}catch(const m::VideoEncodingError&){failed=true;}
                    require(failed==fail,"Pipeline encoder error was swallowed.");std::ifstream old(destination);std::string bytes{std::istreambuf_iterator<char>(old),{}};require(bytes=="preserve-existing","Pipeline failure/cancel replaced destination.");}
                std::cout<<"decoded_pipeline_self_test=passed color_cases=12 input_codecs=2 output_codecs=2 independent_modes=4\n";
            }else for(int trial=1;trial<=(benchmark?3:1);++trial)for(bool native:{false,true}){
                if(!benchmark && ((mode=="native" && !native)||(mode=="rgba" && native)))continue;
                const bool native_input=benchmark?native:input_mode=="native";
                try {auto result=runDecoded(session,source,root/(std::string("decoded-")+(native_input?"native-in-":"rgba-in-")+(native?"native-out-":"rgba-out-")+std::to_string(w)+"-"+std::to_string(trial)+".mkv"),output_codec.toStdString(),native_input,native,benchmark?30:0,benchmark?180:12,!benchmark,cancel_at,-1,benchmark?1:2);
                if(result.cancelled){std::cout<<"decoded_cancelled=1\n";return;}
                std::cout<<"decoded_benchmark input="<<(native_input?"native":"rgba")<<" output="<<(native?"native":"rgba")<<" width="<<width<<" height="<<height<<" trial="<<trial<<" total_ms="<<result.total_ms<<" fps="<<(benchmark?180:12)*1000/result.total_ms
                    <<" input_mean_ms="<<result.input_mean_ms<<" input_p95_ms="<<result.input_p95_ms<<" input_completion_mean_ms="<<result.input_completion_mean_ms<<" input_completion_p95_ms="<<result.input_completion_p95_ms<<" output_mean_ms="<<result.output_mean_ms<<" output_p95_ms="<<result.output_p95_ms
                    <<" decoded_download_frames="<<result.decode.downloaded_frames<<" decoded_download_bytes="<<result.decode.downloaded_bytes<<" hardware_frames="<<result.decode.hardware_frames
                    <<" source_upload_bytes="<<result.source_upload<<" output_readback_bytes="<<result.output_readback<<" encoder_upload_bytes="<<result.encoder_upload<<" input_submissions="<<result.input.submissions<<" input_collections="<<result.input.collections
                    <<" input_copy_bytes="<<result.input.nv12_copy_bytes<<" input_blit_bytes="<<result.input.bgra_blit_bytes<<" input_submit_ns="<<result.input.submit_nanoseconds<<" input_wait_ns="<<result.input.wait_nanoseconds<<" conversion_ns="<<result.input.conversion_nanoseconds
                    <<" output_blit_bytes="<<result.output.vulkan_blit_bytes<<" output_copy_bytes="<<result.output.d3d_copy_bytes<<" output_submit_ns="<<result.output.submit_nanoseconds<<" output_wait_ns="<<result.output.wait_nanoseconds
                    <<" known_peak="<<result.known_peak<<" decoder_peak="<<result.decode.peak_reserved_gpu_bytes<<" combined_peak_estimate="<<result.known_peak+result.decode.peak_reserved_gpu_bytes<<" active_slots="<<result.active_slots<<" peak_pending="<<result.peak_pending<<"\n"<<std::flush;
                }catch(const DecodedUnavailable& e){if(!benchmark)throw;std::cout<<"decoded_unavailable input="<<(native_input?"native":"rgba")<<" output="<<(native?"native":"rgba")<<" width="<<width<<" height="<<height<<" trial="<<trial<<" reason="<<e.what()<<"\n"<<std::flush;}
            }
        }catch(...){failure=std::current_exception();}});worker.join();if(failure)std::rethrow_exception(failure);
    }
    qunsetenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY");return 0;
}
}
