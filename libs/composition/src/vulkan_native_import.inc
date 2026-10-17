// Included after VulkanDevice::Impl and the output bridge's Win32 helpers.
namespace creative_suite::composition {
struct VulkanNativeImportBridge::Impl {
    std::shared_ptr<VulkanDevice> device;
    Metrics metrics;
    bool failed=false,retirement_failed=false;
#ifdef CS_VULKAN_NATIVE_EXPORT
    struct Shared {
        VulkanDevice::Impl* owner=nullptr;
        // Destroy imported backing before releasing its D3D reservation/device.
        std::shared_ptr<void> reservation,backing_reservation;
        detail::WindowsNv12Converter converter;
        ComPtr<IDXGIKeyedMutex> mutex;
        ComPtr<ID3D11Query> conversion_done;
        VkImage image=VK_NULL_HANDLE;
        VkImageView view=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        std::uint64_t bytes=0;
        ~Shared(){if(owner){std::lock_guard lock(owner->mutex);
            if(view)vkDestroyImageView(owner->device,view,nullptr);
            if(image)vkDestroyImage(owner->device,image,nullptr);
            if(memory)vkFreeMemory(owner->device,memory,nullptr);
            owner->allocation_bytes.erase(this);owner->bytes-=bytes;}}
    };
    struct Slot {
        std::shared_ptr<Shared> shared;
        media::NativeVideoFramePtr source;
        GpuTextureFramePtr output;
        std::shared_ptr<VulkanDevice::Impl::Completion> completion;
        VulkanDevice::Impl::Context* context=nullptr;
        std::uint64_t ticket=0;
        bool d3d_ready=false,d3d_acquired=false;
    };
    std::vector<Slot> input_slots;
    std::deque<std::uint64_t> order;
    bool direct_sampling=false;
    int width=0,height=0;
    unsigned storage_width=0,storage_height=0;
    void* source_device=nullptr;
    void finish(Slot& slot,const GpuFrameCompositor::CancellationPredicate& cancel){
        const auto started=Clock::now();
        if(slot.completion){auto& p=*device->impl_;std::unique_lock lock(p.mutex);
            p.reap();if(!slot.completion->done)p.wait(*slot.context,lock,cancel);
        }else if(slot.d3d_acquired){
            for(;;){cancelled(cancel);HRESULT hr=S_FALSE;
                slot.source->with_device_lock([&]{hr=slot.shared->converter.commands->GetData(slot.shared->conversion_done.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);});
                checkHr(hr,"Retire decoder conversion query.");if(hr==S_OK)break;
                if(ns(started)>10'000'000'000ULL)throw Failure("Decoder conversion query timed out.",VK_TIMEOUT);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            checkHr(slot.shared->mutex->ReleaseSync(0),"Release retired decoder conversion.");
        }else if(slot.d3d_ready){
            // Conversion was issued but Vulkan submission failed. Acquire key 1
            // confirms its D3D consumer has stopped before resetting the slot.
            for(;;){cancelled(cancel);auto hr=slot.shared->mutex->AcquireSync(1,0);
                if(hr==S_OK){checkHr(slot.shared->mutex->ReleaseSync(0),"Retire unsubmitted decoder conversion.");break;}
                if(hr!=WAIT_TIMEOUT)checkHr(hr,"Retire decoder keyed mutex.");
                if(ns(started)>10'000'000'000ULL)throw Failure("Decoder conversion retirement timed out.",VK_TIMEOUT);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        metrics.wait_nanoseconds+=ns(started);
    }
    std::shared_ptr<VulkanDevice::Impl::Completion> submitCommands(Slot& slot,VulkanDevice::Impl::Context* context){
        const std::uint64_t acquire_key=1,release_key=0;const uint32_t timeout=10000;
        VkWin32KeyedMutexAcquireReleaseInfoKHR sync{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};sync.acquireCount=sync.releaseCount=1;
        sync.pAcquireSyncs=sync.pReleaseSyncs=&slot.shared->memory;sync.pAcquireKeys=&acquire_key;sync.pReleaseKeys=&release_key;sync.pAcquireTimeouts=&timeout;
        // Even a failed queue submit cannot authorize reuse: retain its original
        // fence receipt so unconfirmed device retirement enters quarantine.
        slot.context=context;slot.completion=context->completion;
        return device->impl_->finish(context,VK_NULL_HANDLE,VK_NULL_HANDLE,&sync);
    }
    void retire(Slot& slot){slot.source.reset();slot.output.reset();slot.completion.reset();slot.context=nullptr;slot.ticket=0;slot.d3d_ready=slot.d3d_acquired=false;}
#endif
    explicit Impl(std::shared_ptr<VulkanDevice> value):device(std::move(value)){}
};
VulkanNativeImportBridge::VulkanNativeImportBridge(std::shared_ptr<VulkanDevice> device):impl_(std::make_unique<Impl>(std::move(device))){}
VulkanNativeImportBridge::~VulkanNativeImportBridge(){discard();if(!isRetired()){
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan-native-import","retain-unconfirmed-resources",
        "Decoder bridge resources and reservations remain retained until process exit after unconfirmed retirement.");
    static_cast<void>(impl_.release());}}
std::optional<std::array<std::uint8_t,8>> VulkanNativeImportBridge::adapterLuid(const media::NativeVideoFramePtr& frame,std::string& cause){
#ifdef CS_VULKAN_NATIVE_EXPORT
    try{if(!frame || !frame->d3d11_view().device)throw Failure("A native decoder device is required.",0,Status::Unsupported);
        return adapterIdentity(static_cast<ID3D11Device*>(frame->d3d11_view().device));}catch(const std::exception& e){cause=e.what();return {};}
#else
    Q_UNUSED(frame);cause="Native decoder adapter identity is unavailable.";return {};
#endif
}
GpuCompositionResult VulkanNativeImportBridge::prepare(const media::NativeVideoFramePtr& frame,unsigned maximum,bool reject){
    discard();if(!isRetired())return {Status::Failed,{},"prepare-native-import","Previous input resources have not retired."};
    try {
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;b.input_slots.clear();b.failed=false;
        if(!b.device || !b.device->valid() || !b.device->impl_->native_export)
            throw Failure("Experimental Win32 interop is unavailable.",0,Status::Unsupported);
        if(!frame || !maximum || maximum>2)throw Failure("Invalid native input or slot count.",0,Status::Unsupported);
        auto view=frame->d3d11_view();
        if(reject || !view.device || !view.texture || view.format!=media::NativeVideoFormat::Nv12 || view.width<=0 || view.height<=0)
            throw Failure("Native input requires an identity-oriented NV12 decoder frame.",0,Status::Unsupported);
        auto* texture=static_cast<ID3D11Texture2D*>(view.texture);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(desc.Format!=DXGI_FORMAT_NV12 || view.array_slice>=desc.ArraySize || unsigned(view.width)>desc.Width || unsigned(view.height)>desc.Height)
            throw Failure("Invalid decoder array slice or visible extent.",0,Status::Unsupported);
        auto& p=*b.device->impl_;
        {
            std::lock_guard lock(p.mutex);
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&id;vkGetPhysicalDeviceProperties2(p.physical,&props);
            auto luid=adapterIdentity(static_cast<ID3D11Device*>(view.device));
            if(!id.deviceLUIDValid || std::memcmp(id.deviceLUID,luid.data(),luid.size()))throw Failure("Decoder and Vulkan use different adapters.",0,Status::Unsupported);
        }
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};query.pNext=&external;
        query.format=VK_FORMAT_B8G8R8A8_UNORM;query.type=VK_IMAGE_TYPE_2D;query.tiling=VK_IMAGE_TILING_OPTIMAL;
        query.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        VkExternalImageFormatProperties external_props{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};properties.pNext=&external_props;
        auto result=vkGetPhysicalDeviceImageFormatProperties2(p.physical,&query,&properties);
        VkFormatProperties bgra{},rgba{};vkGetPhysicalDeviceFormatProperties(p.physical,VK_FORMAT_B8G8R8A8_UNORM,&bgra);vkGetPhysicalDeviceFormatProperties(p.physical,VK_FORMAT_R8G8B8A8_UNORM,&rgba);
        if(result!=VK_ERROR_FORMAT_NOT_SUPPORTED)check(result,"Query sampled decoder format.");
        b.direct_sampling=result==VK_SUCCESS && (external_props.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) &&
            (bgra.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_IMPORT_BLIT")!=1;
        if(!b.direct_sampling){
            query.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            result=vkGetPhysicalDeviceImageFormatProperties2(p.physical,&query,&properties);
            if(result==VK_ERROR_FORMAT_NOT_SUPPORTED || !(external_props.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
                !(bgra.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !(rgba.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
                throw Failure("Shared decoder BGRA import/blit is unsupported.",0,Status::Unsupported);
            check(result,"Query shared decoder format.");
        }
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Info,"vulkan-native-import","conversion-path",
            b.direct_sampling?"Direct sampled BGRA to RGBA8_UINT.":"Legacy blit normalization selected: sampled import unavailable or diagnostic override.");
        b.width=view.width;b.height=view.height;b.source_device=view.device;b.storage_width=desc.Width;b.storage_height=desc.Height;
        {
            std::lock_guard lock(p.mutex);const auto rgba_bytes=std::uint64_t(b.width)*b.height*4;
            const auto flight_bytes=std::uint64_t(desc.Width)*desc.Height*3/2+32+rgba_bytes*(b.direct_sampling?2:3);
            p.trim(flight_bytes*maximum);
            while(maximum>1 && p.bytes+flight_bytes*maximum>p.capacity)--maximum;
            if(p.bytes+flight_bytes>p.capacity)throw Failure("No decoder import flight fits the known budget.",0,Status::Busy);
        }
        for(unsigned i=0;i<maximum;++i){
            auto shared=std::make_shared<Impl::Shared>();shared->owner=&p;
            // NV12 copy plus small shader constants. Imported BGRA is charged
            // by its Vulkan allocation only, never a second external estimate.
            shared->reservation=b.device->tryReserveExternal(std::uint64_t(desc.Width)*desc.Height*3/2+32);
            if(!shared->reservation){if(i)break;throw Failure("Decoder bridge capacity is occupied.",0,Status::Busy);}
            shared->backing_reservation=b.device->tryReserveExternal(std::uint64_t(b.width)*b.height*4);
            if(!shared->backing_reservation){if(i)break;throw Failure("Shared decoder backing capacity is occupied.",0,Status::Busy);}
            frame->with_device_lock([&]{checkHr(shared->converter.create(view,desc,true),"Create decoder conversion textures.");
                D3D11_QUERY_DESC event{D3D11_QUERY_EVENT,0};checkHr(shared->converter.device->CreateQuery(&event,&shared->conversion_done),"Create decoder retirement query.");});
            checkHr(shared->converter.rgba.As(&shared->mutex),"Query decoder shared mutex.");
            ComPtr<IDXGIResource1> resource;checkHr(shared->converter.rgba.As(&resource),"Query decoder shared resource.");
            struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}} handle;
            checkHr(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"Create decoder NT handle.");
            std::lock_guard lock(p.mutex);
            VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ext.handleTypes=external.handleType;
            VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.pNext=&ext;image.imageType=VK_IMAGE_TYPE_2D;image.format=query.format;
            image.extent={unsigned(b.width),unsigned(b.height),1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=query.tiling;image.usage=query.usage;
            check(vkCreateImage(p.device,&image,nullptr,&shared->image),"Create imported decoder image.");
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(p.device,shared->image,&req);
            // Transfer the shared backing estimate into the imported allocation
            // charge under the same admission lock; it is not a second allocation.
            shared->backing_reservation.reset();p.trim(req.size);
            if(p.bytes+req.size>p.capacity){if(i)break;throw Failure("Imported decoder backing exceeds capacity.",0,Status::Busy);}
            auto get=reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vkGetDeviceProcAddr(p.device,"vkGetMemoryWin32HandlePropertiesKHR"));
            if(!get)throw Failure("Win32 import entry point is unavailable.",0,Status::Unsupported);
            VkMemoryWin32HandlePropertiesKHR bits{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};check(get(p.device,external.handleType,handle.value,&bits),"Query decoder memory type.");
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=shared->image;
            VkImportMemoryWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};imported.pNext=&dedicated;imported.handleType=external.handleType;imported.handle=handle.value;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&imported;allocation.allocationSize=req.size;allocation.memoryTypeIndex=p.memoryType(req.memoryTypeBits&bits.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(p.device,&allocation,nullptr,&shared->memory),"Import decoder shared allocation.");shared->bytes=req.size;p.bytes+=req.size;p.peak=std::max(p.peak,p.bytes);p.known_peak=std::max(p.known_peak,p.bytes+p.external_reserved);p.registerAllocation(shared.get(),req.size,"import");
            check(vkBindImageMemory(p.device,shared->image,shared->memory,0),"Bind decoder shared allocation.");
            if(b.direct_sampling){VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view_info.image=shared->image;
                view_info.viewType=VK_IMAGE_VIEW_TYPE_2D;view_info.format=query.format;view_info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                check(vkCreateImageView(p.device,&view_info,nullptr,&shared->view),"Create sampled decoder view.");}
            Impl::Slot slot;slot.shared=std::move(shared);b.input_slots.push_back(std::move(slot));
        }
        return {Status::Complete};
#else
        Q_UNUSED(frame);Q_UNUSED(maximum);Q_UNUSED(reject);return {Status::Unsupported,{},"prepare-native-import","The Windows Vulkan decoder bridge is unavailable."};
#endif
    }catch(const Failure& e){
#ifdef CS_VULKAN_NATIVE_EXPORT
        impl_->input_slots.clear();
#endif
        return {e.status,{},"prepare-native-import",e.what(),e.code};
    }catch(const std::exception& e){return {Status::Failed,{},"prepare-native-import",e.what()};}
}
VulkanNativeImportResult VulkanNativeImportBridge::submit(const media::NativeVideoFramePtr& frame,const GpuFrameCompositor::CancellationPredicate& cancel,unsigned fail){
#ifdef CS_VULKAN_NATIVE_EXPORT
    VulkanDevice::Impl::Context* context=nullptr;Impl::Slot* active_slot=nullptr;bool external_commands=false;
#endif
    try {cancelled(cancel);
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;if(b.failed)throw Failure("The decoder bridge failed; recreate resources.");
        if(!frame || b.input_slots.empty())throw Failure("The decoder bridge is not prepared.",0,Status::Unsupported);
        auto view=frame->d3d11_view();D3D11_TEXTURE2D_DESC desc{};if(view.texture)static_cast<ID3D11Texture2D*>(view.texture)->GetDesc(&desc);
        if(view.device!=b.source_device || view.width!=b.width || view.height!=b.height || view.format!=media::NativeVideoFormat::Nv12 ||
            desc.Width!=b.storage_width || desc.Height!=b.storage_height || view.array_slice>=desc.ArraySize)
            throw Failure("The decoder input does not match its prepared bridge.",0,Status::Unsupported);
        auto slot=std::find_if(b.input_slots.begin(),b.input_slots.end(),[](const auto& s){return !s.ticket;});if(slot==b.input_slots.end())return {Status::Busy};
        auto& p=*b.device->impl_;std::shared_ptr<VulkanImage> display,output;
        {std::lock_guard lock(p.mutex);if(p.current())throw Failure("Input import requires a separate command submission.",0,Status::Busy);
            context=p.start(true,cancel);if(!context)return {Status::Busy};
            if(b.direct_sampling)p.boundary(true);
            else display=p.image(b.device,b.width,b.height,VK_FORMAT_R8G8B8A8_UNORM);
            output=p.image(b.device,b.width,b.height);}
        const auto converted=Clock::now();
        // A free slot's previous Vulkan read has completed and released key 0.
        slot->source=frame;static std::atomic_uint64_t next{1};slot->ticket=next.fetch_add(1);b.order.push_back(slot->ticket);
        auto hr=slot->shared->mutex->AcquireSync(0,0);if(hr!=S_OK)throw Failure("Decoder input mutex is unexpectedly occupied.",hr);
        slot->d3d_acquired=true;
        HRESULT conversion=S_OK;frame->with_device_lock([&]{conversion=slot->shared->converter.convert(view);
            slot->shared->converter.commands->End(slot->shared->conversion_done.Get());slot->shared->converter.commands->Flush();});
        if(fail==1)throw Failure("Injected decoder submission failure while owning D3D.");
        checkHr(slot->shared->mutex->ReleaseSync(1),"Release decoder input to Vulkan.");
        slot->d3d_acquired=false;slot->d3d_ready=true;
        if(fail==3)throw Failure("Injected decoder submission failure after D3D handoff.");
        checkHr(conversion,"Convert decoder NV12 to BGRA.");b.metrics.conversion_nanoseconds+=ns(converted);
        std::lock_guard lock(p.mutex);auto cmd=p.begin();
        const auto ownership=[&](bool acquire){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            const auto owned_layout=b.direct_sampling?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.oldLayout=acquire?VK_IMAGE_LAYOUT_GENERAL:owned_layout;barrier.newLayout=acquire?owned_layout:VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex=acquire?VK_QUEUE_FAMILY_EXTERNAL:p.family;barrier.dstQueueFamilyIndex=acquire?p.family:VK_QUEUE_FAMILY_EXTERNAL;
            const VkAccessFlags read=b.direct_sampling?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcAccessMask=acquire?0:read;barrier.dstAccessMask=acquire?read:0;
            barrier.image=slot->shared->image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);};
        p.current()->resources.push_back(slot->shared);active_slot=&*slot;external_commands=true;ownership(true);
        if(b.direct_sampling){p.dispatchBoundary(true,slot->shared->view,output);}
        else {
            auto& a=p.allocation(display);p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]=region.dstOffsets[1]={b.width,b.height,1};
            vkCmdBlitImage(cmd,slot->shared->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
            p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
            Parameters parameters;parameters.control={10,0,0,0};p.dispatch(output,{},{},output,parameters,display);
            b.metrics.bgra_blit_bytes+=std::uint64_t(b.width)*b.height*4;
        }
        ownership(false);
        if(fail==2)throw Failure("Injected decoder submission failure after recording external commands.");
        const auto submitted=Clock::now();slot->completion=b.submitCommands(*slot,context);context=nullptr;
        b.metrics.submit_nanoseconds+=ns(submitted);slot->output=std::shared_ptr<OpenGlTextureFrame>(new OpenGlTextureFrame(std::move(output)));
        p.traceMemory("input");++b.metrics.submissions;if(b.direct_sampling)++b.metrics.direct_submissions;else ++b.metrics.legacy_submissions;b.metrics.peak_pending=std::max(b.metrics.peak_pending,unsigned(b.order.size()));
        b.metrics.nv12_copy_bytes+=std::uint64_t(desc.Width)*desc.Height*3/2;
        return {Status::Complete,{slot->ticket},slot->output};
#else
        Q_UNUSED(frame);return {Status::Unsupported,{}, {},"The Windows Vulkan decoder bridge is unavailable."};
#endif
    }catch(const Failure& e){
#ifdef CS_VULKAN_NATIVE_EXPORT
        if(context)try{if(external_commands)impl_->submitCommands(*active_slot,context);else impl_->device->impl_->finish(context);}catch(...){impl_->device->impl_->lost=true;}
#endif
        if(e.status==Status::Failed)impl_->failed=true;return {e.status,{}, {},e.what(),e.code};
    }catch(const std::exception& e){
#ifdef CS_VULKAN_NATIVE_EXPORT
        if(context)try{if(external_commands)impl_->submitCommands(*active_slot,context);else impl_->device->impl_->finish(context);}catch(...){impl_->device->impl_->lost=true;}
#endif
        impl_->failed=true;return {Status::Failed,{}, {},e.what()};}
}
VulkanNativeImportResult VulkanNativeImportBridge::collect(VulkanNativeImportTicket ticket,const GpuFrameCompositor::CancellationPredicate& cancel,bool fail){
    try {cancelled(cancel);
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;if(fail){b.failed=true;throw Failure("Injected decoder collection failure.");}if(b.retirement_failed)throw Failure("Decoder retirement is unconfirmed.");
        auto slot=std::find_if(b.input_slots.begin(),b.input_slots.end(),[&](const auto& s){return s.ticket && s.ticket==ticket.value;});
        if(slot==b.input_slots.end())return {Status::Unsupported,{}, {},"Unknown or retired decoder ticket."};
        if(b.order.front()!=ticket.value)return {Status::Busy};b.finish(*slot,cancel);
        auto output=slot->output;b.retire(*slot);b.order.pop_front();++b.metrics.collections;return {Status::Complete,{},std::move(output)};
#else
        Q_UNUSED(ticket);return {Status::Unsupported,{}, {},"Native input is unavailable."};
#endif
    }catch(const Failure& e){return {e.status,{}, {},e.what(),e.code};}
    catch(const std::exception& e){impl_->failed=true;return {Status::Failed,{}, {},e.what()};}
}
void VulkanNativeImportBridge::discard() noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    auto& b=*impl_;if(b.retirement_failed)return;
    for(auto& s:b.input_slots)if(s.ticket)try{b.finish(s,{});b.retire(s);}catch(const std::exception& e){
        b.failed=b.retirement_failed=true;const auto* f=dynamic_cast<const Failure*>(&e);b.metrics.retirement_error_code=f?f->code:0;
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan-native-import","discard",e.what(),{{"error_code",std::to_string(b.metrics.retirement_error_code)},{"ticket",std::to_string(s.ticket)}});}
    if(!b.retirement_failed)b.order.clear();
#endif
}
bool VulkanNativeImportBridge::isRetired() const noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    return std::none_of(impl_->input_slots.begin(),impl_->input_slots.end(),[](const auto& s){return s.ticket!=0;});
#else
    return true;
#endif
}
unsigned VulkanNativeImportBridge::slotCount() const noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    return unsigned(impl_->input_slots.size());
#else
    return 0;
#endif
}
VulkanNativeImportBridge::Metrics VulkanNativeImportBridge::metrics() const noexcept{return impl_->metrics;}
}
