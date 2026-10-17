void runBridge(const rendering::RenderJob& job,const std::atomic_bool& cancel,const rendering::OfflineExportOptions& options,
    rendering::OfflineExportRenderer::ProgressCallback progress={}){
    std::exception_ptr failure;
    std::thread worker([&]{try{rendering::OfflineExportRenderer::render(job,cancel,std::move(progress),options);}catch(...){failure=std::current_exception();}});
    worker.join();if(failure)std::rethrow_exception(failure);
}
// Included inside the existing export test namespace; no separate export engine.
rendering::RenderJob bridgeJob(const OutputChoice& output,const std::filesystem::path& image,const std::filesystem::path& root,int width=1920,int height=1080,int fps=30,double bitrate=10,const char* codec="h264_nvenc",int count=8){
    auto job=makeImageJob(output,image,root/"integrated-native.mkv",1500,count);
    job.settings.width=width;job.settings.height=height;job.settings.frame_rate=fps;job.settings.video_bitrate_mbps=bitrate;
    job.settings.container_name="matroska";job.settings.video_encoder_name=codec;job.settings.export_audio=false;
    job.settings.gpu_composition_enabled=true;job.settings.graphics_backend=creative_suite::composition::GraphicsBackend::Vulkan;
    job.project_snapshot.timeline_frame_rate={fps,1};return job;
}
void inspectBridgeOutput(const rendering::RenderJob& job,int expected){
    auto decoder=media::VideoPlaybackSession::open(pathFromQString(job.settings.output_path));int count=0;
    while(auto frame=decoder->decode_next_frame()){
        require((*frame)->width==job.settings.width && (*frame)->height==job.settings.height,"Native encoder geometry changed.");
        if(expected==4){const auto* pixel=(*frame)->rgba_pixels.data()+((*frame)->height/2)*(*frame)->stride+((*frame)->width/2)*4;
            require(std::abs(int(pixel[0])-210)<=12 && std::abs(int(pixel[1])-40)<=12 && std::abs(int(pixel[2])-30)<=12,"Qualified NVENC output changed reference color beyond tolerance.");}
        if(expected==8 && job.project_snapshot.timeline_tracks.front().clips.size()==2 && job.project_snapshot.timeline_tracks.front().clips.front().kind==timeline::ClipKind::Image){
            const auto* pixel=(*frame)->rgba_pixels.data()+((*frame)->height/2)*(*frame)->stride+((*frame)->width/2)*4;
            require(count<4?pixel[0]>pixel[1]+80:pixel[1]>pixel[0]+80,"Native recovery reordered/duplicated visual frames.");
        }++count;
    }
    require(count==expected,"Native encoder lost or duplicated output frames.");
    AVFormatContext* raw=nullptr;requireFfmpeg(avformat_open_input(&raw,pathFromQString(job.settings.output_path).string().c_str(),nullptr,nullptr),"Inspect native output");
    struct Input{AVFormatContext* p;~Input(){avformat_close_input(&p);}} input{raw};
    requireFfmpeg(avformat_find_stream_info(raw,nullptr),"Inspect native streams");
    AVPacket* packet=av_packet_alloc();require(packet,"Cannot allocate native packet inspector.");
    struct Packet{AVPacket* p;~Packet(){av_packet_free(&p);}} retained{packet};int index=0;
    while(av_read_frame(raw,packet)>=0){
        if(raw->streams[packet->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            require(packet->pts!=AV_NOPTS_VALUE && av_rescale_q(packet->pts,raw->streams[packet->stream_index]->time_base,AVRational{1,int(job.settings.frame_rate)})==index,"Native timestamps changed output order.");++index;
        }av_packet_unref(packet);
    }
    require(index==expected,"Native packet count changed.");
}
void validateNvencPolicy(){
    auto job=bridgeJob(chooseOutput(),{},{});
    require(rendering::qualifiedVulkanNvenc(job.settings),"Default native tuple was rejected.");
    auto equivalent=job.settings;equivalent.frame_rate=30.00000001;equivalent.video_bitrate_mbps=10.0000004;
    require(rendering::qualifiedVulkanNvenc(equivalent),"Equivalent effective encoder parameters were rejected.");
    equivalent.video_bitrate_mbps=10.0000006;require(!rendering::qualifiedVulkanNvenc(equivalent),"A different effective bitrate was accepted.");
    for(const int mode:{0,1,2,3,4,5}){
        auto settings=job.settings;
        if(mode==0)settings.graphics_backend=creative_suite::composition::GraphicsBackend::OpenGl;
        if(mode==1)settings.gpu_composition_enabled=false;
        if(mode==2)settings.video_encoder_name="libx264";
        if(mode==3){settings.width=320;settings.height=180;}
        if(mode==4)settings.frame_rate=29.97;
        if(mode==5)settings.video_bitrate_mbps=10.1;
        require(!rendering::qualifiedVulkanNvenc(settings),"Unqualified native export settings were enabled.");
    }
}
void validateUnconfirmedRetirement(const OutputChoice& output,const std::filesystem::path& image,const std::filesystem::path& root){
    namespace c=creative_suite::composition;
    auto device=c::sessionVulkanDevice();require(device && device->supportsNativeExport(),"Retirement test requires native Vulkan.");
    std::weak_ptr<void> retained;std::exception_ptr failure;
    std::thread worker([&]{try{
        creative_suite::media::NativeVideoFramePool pool(320,180,2);auto reservation=device->tryReserveExternal(pool.reserved_bytes()+320ULL*180*4);require(bool(reservation),"Retirement reservation unavailable.");retained=reservation;
        c::VulkanFrameCompositor compositor(device);auto source=compositor.composeTexture(320,180,{});require(bool(source.frame),"Retirement source unavailable.");
        {c::VulkanNativeExportBridge bridge(device,reservation);auto destination=pool.acquire();require(bridge.prepare(destination).status==c::GpuCompositionStatus::Complete,"Retirement bridge unavailable.");
            require(bridge.submit(source.frame,destination).status==c::GpuCompositionStatus::Complete,"Retirement submission failed.");
            qputenv("CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST","1");bridge.discard();qunsetenv("CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST");
            require(!bridge.isRetired() && bridge.metrics().retirement_error_code!=0,"Unconfirmed consumers were released/reused or lost their error code.");
            require(bridge.prepare(destination).status==c::GpuCompositionStatus::Failed,"An unsafe bridge was reinitialized.");}
        reservation.reset();require(!retained.expired(),"Unconfirmed consumers released their external reservation.");
    }catch(...){failure=std::current_exception();}});worker.join();if(failure)std::rethrow_exception(failure);
    auto job=bridgeJob(output,image,root);const auto destination=pathFromQString(job.settings.output_path);
    {std::ofstream sentinel(destination,std::ios::binary);sentinel<<"preserve-existing";}
    rendering::OfflineExportOptions options;options.vulkan_device=device;options.fail_native_collection_for_testing=3;
    rendering::OfflineExportMetrics metrics;options.metrics_callback=[&](const auto& value){metrics=value;};std::atomic_bool cancel=false;
    qputenv("CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST","1");bool aborted=false;
    try{runBridge(job,cancel,options);}catch(const std::exception&){aborted=true;}qunsetenv("CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST");
    std::ifstream sentinel(destination,std::ios::binary);require(aborted && metrics.encoded_frames==2 && metrics.cpu_frames==0 && metrics.gpu_failures==1 &&
        std::string(std::istreambuf_iterator<char>(sentinel),{})=="preserve-existing","Unconfirmed retirement reused frames or published/restarted the output.");
    std::ifstream diagnostic(logging::Logger::instance().log_path());std::string line;bool error_logged=false;
    while(std::getline(diagnostic,line))if(line.find("operation=\"retire-native-export\"")!=std::string::npos && line.find("error_code=\"2\"")!=std::string::npos)error_logged=true;
    require(error_logged,"Unconfirmed native retirement omitted its actionable error code from the app log.");
    std::cout<<"unconfirmed_native_retirement=held_until_process_exit reservation=retained\n";
}
void validateIntegratedNvenc(const OutputChoice& output,const std::filesystem::path& image,const std::filesystem::path& green,const std::filesystem::path& root,bool qualify){
    namespace c=creative_suite::composition;using namespace rendering;
    auto device=c::sessionVulkanDevice();require(device && device->supportsNativeExport(),"Native Vulkan interop was not enabled.");
    // RAII leases compose with preexisting preview allocations and are shareable.
    const auto before=device->totalReservedBytes();
    auto first=device->tryReserveExternal(1024*1024),second=device->tryReserveExternal(2*1024*1024);
    require(first && second && device->totalReservedBytes()==before+3*1024*1024,"Concurrent external reservations were not counted.");
    auto retained=first;first.reset();require(device->totalReservedBytes()==before+3*1024*1024,"A retained budget lease released early.");
    require(!device->tryReserveExternal(256ULL*1024*1024),"External reservation exceeded the session cap.");
    second.reset();retained.reset();require(device->totalReservedBytes()==before,"External reservations leaked.");
    std::atomic_bool cancel=false;OfflineExportMetrics metrics;OfflineExportOptions options;options.vulkan_device=device;
    options.metrics_callback=[&](const auto& value){metrics=value;};
    auto job=bridgeJob(output,image,root);int callbacks=0,rgba_callbacks=0;
    job.project_snapshot.timeline_tracks.front().clips.front().duration_frames=4;
    auto next=job.project_snapshot.timeline_tracks.front().clips.front();next.source_path=green;next.timeline_start_frame=4;job.project_snapshot.timeline_tracks.front().clips.push_back(next);
    options.prepared_native_frame_callback=[&](const auto&,auto){++callbacks;};options.prepared_frame_callback=[&](const auto&,auto){++rgba_callbacks;};
    runBridge(job,cancel,options);
    require(metrics.native_encoded_frames==8 && metrics.native_submissions==8 && metrics.native_collections==8 && metrics.readback_bytes==0 && metrics.encoding_uploaded_bytes==0 && callbacks==8 && rgba_callbacks==0,"Integrated NVENC silently used an RGBA path.");
    require(metrics.peak_pending_frames==2 && metrics.peak_known_gpu_bytes<=256ULL*1024*1024,"Native FIFO/budget contract changed.");inspectBridgeOutput(job,8);
    for(int mode=0;mode<6;++mode){
        auto fault=options;fault.prepared_native_frame_callback={};fault.prepared_frame_callback={};callbacks=rgba_callbacks=0;
        if(mode==0)fault.fail_native_submission_for_testing=2;
        if(mode==1)fault.fail_native_collection_for_testing=3;
        if(mode==2)fault.native_maximum_frames=1;
        if(mode==3)fault.reject_native_format_for_testing=true;
        if(mode==4)job.settings.video_bitrate_mbps=10.1;
        if(mode==5)qputenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA","1");
        std::vector<std::int64_t> positions;
        fault.prepared_native_frame_callback=[&](const auto&,auto at){positions.push_back(at);};fault.prepared_frame_callback=[&](const auto&,auto at){positions.push_back(at);};
        runBridge(job,cancel,fault);qunsetenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA");job.settings.video_bitrate_mbps=10;
        require(metrics.encoded_frames==8 && positions==std::vector<std::int64_t>({0,1,2,3,4,5,6,7}),"Native recovery duplicated callbacks or frames.");
        if(mode<2)require(metrics.gpu_failures==1 && metrics.cpu_frames>0 && metrics.encoding_uploaded_bytes>0,"Bridge failure did not use bounded CPU recovery.");
        if(mode==2)require(metrics.native_encoded_frames==8 && metrics.peak_pending_frames==1,"Explicit one-flight export changed.");
        if(mode>=3)require(metrics.native_encoded_frames==0 && !metrics.native_fallback_reason.empty(),"Unqualified/unavailable native path was used.");
        inspectBridgeOutput(job,8);
    }
    // The export boundary now needs three images per flight, rather than four.
    for(const auto mib:{196ULL,250ULL}){
        auto occupied=device->tryReserveExternal(mib*1024*1024);require(bool(occupied),"Cannot reserve controlled memory pressure.");
        auto pressure=options;pressure.prepared_native_frame_callback={};pressure.prepared_frame_callback={};
        runBridge(job,cancel,pressure);
        if(mib==196)require(metrics.native_encoded_frames==8 && metrics.native_slots==1 && metrics.peak_pending_frames==1,"Memory pressure did not reduce native flights.");
        else require(metrics.native_encoded_frames==0 && metrics.gpu_failures==0 && metrics.encoded_frames==8,"Capacity pressure became a technical failure.");
    }
    for(const auto codec:{"h264_nvenc","hevc_nvenc"}){
        auto tiny=bridgeJob(output,image,root,320,180,30,10,codec,4);auto compatible=options;
        compatible.prepared_native_frame_callback={};compatible.prepared_frame_callback={};runBridge(tiny,cancel,compatible);
        require(metrics.native_encoded_frames==0 && metrics.encoded_frames==4 && metrics.native_fallback_reason=="unqualified-settings","Unqualified small NVENC output entered native sharing.");
        inspectBridgeOutput(tiny,4);
    }
    // Preserve existing bytes after cancellation with native consumers pending.
    const auto destination=pathFromQString(job.settings.output_path);{std::ofstream old(destination,std::ios::binary);old<<"sentinel";}
    auto interrupted=options;interrupted.prepared_native_frame_callback=[&](const auto&,auto){cancel=true;};
    bool cancelled=false;try{runBridge(job,cancel,interrupted);}catch(const ExportCanceled&){cancelled=true;}cancel=false;
    std::ifstream old(destination,std::ios::binary);require(cancelled && std::string(std::istreambuf_iterator<char>(old),{})=="sentinel","Native cancellation published an incomplete destination.");old.close();
    auto failed=options;failed.fail_encoder_after_for_testing=2;bool encoder_failed=false;
    try{runBridge(job,cancel,failed);}catch(const ExportError&){encoder_failed=true;}
    {std::ifstream preserved(destination,std::ios::binary);require(encoder_failed && std::string(std::istreambuf_iterator<char>(preserved),{})=="sentinel","Encoder failure restarted or replaced the destination.");}
    validateActiveQueueShutdown(output,image,root,true);
    // CPU oracle for transforms, effects, masks and Fusion, outside benchmark.
    auto& clip=job.project_snapshot.timeline_tracks.front().clips.front();clip.kind=timeline::ClipKind::FusionComposition;
    auto graph=fusion::nodes::makeCompositionGraph();graph.nodes[0].background_color={180,40,20,180};
    fusion::nodes::Node mask;mask.id=3;mask.type=fusion::nodes::NodeType::EllipseMask;graph.nodes.push_back(mask);graph.connections.push_back({3,2,2});graph.next_id=4;
    clip.node_graph=graph;clip.transform.rotation_degrees=23;clip.transform.opacity=.7;
    clip.effects={creative_suite::effects::makeDefaultInstance("video.brightness"),creative_suite::effects::makeDefaultInstance("video.saturation")};
    require(creative_suite::effects::setParameterValue(clip.effects[0],"amount",7) && creative_suite::effects::setParameterValue(clip.effects[1],"amount",80),"Cannot prepare ordered effects.");
    std::vector<media::VideoFrame> reference;auto cpu=job;cpu.settings.graphics_backend=c::GraphicsBackend::Cpu;cpu.settings.gpu_composition_enabled=false;cpu.settings.video_encoder_name="ffv1";cpu.settings.output_path=pathToQString(root/"cpu-oracle.mkv");
    OfflineExportOptions oracle;oracle.prepared_frame_callback=[&](const auto& frame,auto){reference.push_back(frame);};runBridge(cpu,cancel,oracle);
    auto verification=options;verification.prepared_frame_callback={};verification.prepared_native_frame_callback=[&](const auto& frame,std::int64_t at){
        auto pixels=frame->download_rgba();const auto& expected=reference.at(std::size_t(at));require(pixels->rgba_pixels.size()==expected.rgba_pixels.size(),"Native Fusion geometry changed.");
        for(std::size_t i=0;i<expected.rgba_pixels.size();++i)require(std::abs(int(pixels->rgba_pixels[i])-int(expected.rgba_pixels[i]))<=(i%4==3?0:2),"Native Fusion pixel parity changed.");};
    runBridge(job,cancel,verification);require(metrics.native_encoded_frames==8,"Fusion did not reach native encoding.");
    {auto reader=media::VideoPlaybackSession::open(pathFromQString(job.settings.output_path));std::size_t count=0;
        while(auto frame=reader->decode_next_frame()){require(count<reference.size(),"Native Fusion encoder duplicated a frame.");double difference=0;
            for(std::size_t i=0;i<reference[count].rgba_pixels.size();++i)if(i%4!=3)difference+=std::abs(int((*frame)->rgba_pixels[i])-int(reference[count].rgba_pixels[i]));
            require(difference/(job.settings.width*job.settings.height*3)<12,"Native Fusion encoding exceeded the current lossy RGB tolerance.");++count;}
        require(count==reference.size(),"Native Fusion encoder dropped a frame.");}reference.clear();
    // Actual embedded audio retains the existing mix/output contract.
    auto audio_job=bridgeJob(output,createVideoWithAudioFixture(root),root,1920,1080,30,10,"h264_nvenc",30);
    audio_job.project_snapshot.timeline_tracks.front().clips.front().kind=timeline::ClipKind::Video;audio_job.settings.export_audio=true;
    audio_job.project_snapshot.timeline_tracks.front().audio_gain=.5;
    auto& audio_clip=audio_job.project_snapshot.timeline_tracks.front().clips.front();audio_clip.source_start_frame=30;audio_clip.source_duration_frames=30;audio_clip.audio_gain=.5;
    auto audio_options=options;audio_options.prepared_native_frame_callback={};audio_options.prepared_frame_callback={};runBridge(audio_job,cancel,audio_options);
    require(metrics.native_encoded_frames==30 && decodedAudioRms(pathFromQString(audio_job.settings.output_path))>0.025,"Native export lost embedded audio.");inspectBridgeOutput(audio_job,30);
    if(qualify){
        int tuples=0;
        for(const auto size:{QSize(1280,720),QSize(1920,1080),QSize(1080,1920),QSize(2560,1440),QSize(3840,2160)})for(const int fps:{30,60})for(const double baseline:{5.0,10.0,20.0})for(const auto codec:{"h264_nvenc","hevc_nvenc"}){
            const auto bitrate=std::clamp(std::round(baseline*(double(size.width())*size.height()*fps)/(1920.0*1080*30)*10)/10,0.1,500.0);
            auto entry=bridgeJob(output,image,root,size.width(),size.height(),fps,bitrate,codec,4);
            require(qualifiedVulkanNvenc(entry.settings),"A preset tuple was not in qualification policy.");
            auto policy=options;policy.prepared_frame_callback={};policy.prepared_native_frame_callback={};runBridge(entry,cancel,policy);
            require(metrics.native_encoded_frames==4 && metrics.readback_bytes==0 && metrics.encoding_uploaded_bytes==0,"A qualified tuple failed native delivery.");inspectBridgeOutput(entry,4);++tuples;
            std::cout<<"qualified codec="<<codec<<" width="<<size.width()<<" height="<<size.height()<<" fps="<<fps<<" bitrate="<<bitrate<<" frames=4 slots="<<metrics.native_slots<<" known_peak="<<metrics.peak_known_gpu_bytes<<"\n"<<std::flush;
        }
        require(tuples==60,"Qualification matrix was incomplete.");
    }
    std::cout<<"integrated_native_nvenc=passed fifo=2 recovery=bounded rgba_unqualified=1 audio=1 fusion_parity=1\n";
}
void benchmarkIntegratedNvenc(const OutputChoice& output,const std::filesystem::path& image,const std::filesystem::path& root){
    using Clock=std::chrono::steady_clock;
    (void)image;qputenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY","1");
    QImage background(512,288,QImage::Format_RGBA8888),overlay(256,144,QImage::Format_RGBA8888);
    for(auto* frame:{&background,&overlay})for(int y=0;y<frame->height();++y)for(int x=0;x<frame->width();++x){auto* pixel=frame->scanLine(y)+x*4;
        pixel[0]=uchar(x*13+y*7);pixel[1]=uchar(x*5+y*17);pixel[2]=uchar(x*29+y*3);pixel[3]=frame==&background?255:uchar(x*31+y*11);}
    const auto background_path=root/"native-benchmark-source.png",overlay_path=root/"native-benchmark-overlay.png";
    require(background.save(pathToQString(background_path)) && overlay.save(pathToQString(overlay_path)),"Cannot write native benchmark fixtures.");
    for(const auto size:{QSize(1920,1080),QSize(2560,1440),QSize(3840,2160)})for(int trial=1;trial<=3;++trial)for(bool native:{false,true}){
        const auto bitrate=std::round(10.0*(double(size.width())*size.height())/(1920.0*1080)*10)/10;
        auto job=bridgeJob(output,background_path,root,size.width(),size.height(),30,bitrate,"h264_nvenc",210);
        auto top=job.project_snapshot.timeline_tracks.front().clips.front();top.source_path=overlay_path;top.transform.scale=.72;top.transform.rotation_degrees=23;
        top.effects={creative_suite::effects::makeDefaultInstance("video.brightness"),creative_suite::effects::makeDefaultInstance("video.saturation")};
        require(creative_suite::effects::setParameterValue(top.effects[0],"amount",7) && creative_suite::effects::setParameterValue(top.effects[1],"amount",80),"Cannot prepare benchmark effects.");
        project::ProjectTrack track;track.track_id=2;track.clips={top};job.project_snapshot.timeline_tracks.insert(job.project_snapshot.timeline_tracks.begin(),track);
        top.transform.rotation_degrees=0;top.transform.position_x=.7;top.transform.opacity=.6;top.effects.clear();track.track_id=3;track.clips={top};job.project_snapshot.timeline_tracks.insert(job.project_snapshot.timeline_tracks.begin(),track);
        if(native)qunsetenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA");else qputenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA","1");
        rendering::OfflineExportOptions options;options.vulkan_device=creative_suite::composition::sessionVulkanDevice();
        // Equivalent FIFO width under the shared budget including recovery staging.
        options.native_maximum_frames=size.width()==3840?1:2;
        std::vector<double> delivery;Clock::time_point start;rendering::OfflineExportMetrics metrics;
        options.delivery_callback=[&](auto index,auto ns){if(index==30)start=Clock::now()-std::chrono::nanoseconds(ns);if(index>=30)delivery.push_back(ns/1e6);};
        options.metrics_callback=[&](const auto& value){metrics=value;};std::atomic_bool cancel=false;
        runBridge(job,cancel,options);const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(native)require(metrics.native_encoded_frames==210 && metrics.readback_bytes==0 && metrics.encoding_uploaded_bytes==0,"Native benchmark fell back.");
        require(delivery.size()==180 && metrics.peak_pending_frames==options.native_maximum_frames,"Measured samples or equivalent parallelism changed.");double total=0;for(auto value:delivery)total+=value;std::sort(delivery.begin(),delivery.end());
        std::cout<<"integrated_benchmark mode="<<(native?"native":"rgba")<<" width="<<size.width()<<" height="<<size.height()<<" trial="<<trial<<" total_ms="<<elapsed<<" fps="<<180000/elapsed
            <<" delivery_mean_ms="<<total/180<<" delivery_p95_ms="<<delivery[170]<<" preparation_ns="<<metrics.preparation_nanoseconds<<" encoding_ns="<<metrics.encoding_nanoseconds<<" finalization_ns="<<metrics.finalization_nanoseconds<<" uploaded_bytes="<<metrics.uploaded_bytes<<" native_frames="<<metrics.native_encoded_frames<<" readback_bytes="<<metrics.readback_bytes<<" encoding_upload_bytes="<<metrics.encoding_uploaded_bytes
            <<" blit_bytes="<<metrics.native_blit_bytes<<" copy_bytes="<<metrics.native_copy_bytes<<" submit_ns="<<metrics.native_submit_nanoseconds<<" wait_ns="<<metrics.native_wait_nanoseconds<<" known_peak_bytes="<<metrics.peak_known_gpu_bytes<<" pending="<<metrics.peak_pending_frames<<"\n"<<std::flush;
    }qunsetenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA");qunsetenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY");
}
