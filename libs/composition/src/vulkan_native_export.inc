// Private implementation included after VulkanDevice::Impl. This keeps Vulkan,
// Win32 and COM handles out of the public experimental contract.
namespace creative_suite::composition {
#ifdef CS_VULKAN_NATIVE_EXPORT
namespace {
using Microsoft::WRL::ComPtr;
void checkHr(HRESULT hr,const char* operation) {
    if(FAILED(hr))throw Failure(operation,static_cast<std::int64_t>(hr));
}
std::array<std::uint8_t,8> adapterIdentity(ID3D11Device* device,bool require_nvenc=false) {
    ComPtr<IDXGIDevice> dxgi;checkHr(device->QueryInterface(IID_PPV_ARGS(&dxgi)),"Query encoder DXGI device.");
    ComPtr<IDXGIAdapter> adapter;checkHr(dxgi->GetAdapter(&adapter),"Query encoder adapter.");
    DXGI_ADAPTER_DESC desc{};checkHr(adapter->GetDesc(&desc),"Query encoder adapter identity.");
    if(require_nvenc && desc.VendorId!=0x10DE)throw Failure("Native NVENC sharing requires an NVIDIA encoder adapter.",0,Status::Unsupported);
    std::array<std::uint8_t,8> luid;static_assert(sizeof(desc.AdapterLuid)==luid.size());
    std::memcpy(luid.data(),&desc.AdapterLuid,luid.size());return luid;
}
void checkNativeDestination(const media::NativeVideoFramePtr& frame) {
    if(!frame)throw Failure("A native encoder frame is required.",0,Status::Unsupported);
    const auto view=frame->d3d11_view();
    if(!view.device || !view.texture || view.array_slice || view.format!=media::NativeVideoFormat::Bgra8)
        throw Failure("Native export requires a single BGRA8 D3D11 texture.",0,Status::Unsupported);
}
}
#endif
struct VulkanNativeExportBridge::Impl {
    std::shared_ptr<VulkanDevice> device;
    std::shared_ptr<void> external_reservation;
    Metrics metrics;
    bool retirement_failed=false;
    bool failed=false;
#ifdef CS_VULKAN_NATIVE_EXPORT
    struct Imported {
        VulkanDevice::Impl* owner=nullptr;
        VkImage image=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        std::uint64_t bytes=0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<IDXGIKeyedMutex> mutex;
        ~Imported(){
            if(!owner)return;std::lock_guard lock(owner->mutex);
            if(image)vkDestroyImage(owner->device,image,nullptr);
            if(memory)vkFreeMemory(owner->device,memory,nullptr);
            owner->bytes-=bytes;
        }
    };
    struct Slot {
        std::shared_ptr<Imported> imported;
        ComPtr<ID3D11Query> copy_done;
        std::shared_ptr<VulkanDevice::Impl::Completion> completion;
        VulkanDevice::Impl::Context* context=nullptr;
        GpuTextureFramePtr source;
        media::NativeVideoFramePtr destination;
        std::uint64_t ticket=0;
        bool acquired=false,copy_issued=false,ready=false;
    };
    std::vector<Slot> native_slots;
    std::deque<std::uint64_t> order;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> commands;
    int width=0,height=0;
    void ownership(VkCommandBuffer cmd,VkImage image,bool acquire) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask=acquire?0:VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=acquire?VK_ACCESS_TRANSFER_WRITE_BIT:0;
        barrier.oldLayout=acquire?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=acquire?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=acquire?VK_QUEUE_FAMILY_EXTERNAL:device->impl_->family;
        barrier.dstQueueFamilyIndex=acquire?device->impl_->family:VK_QUEUE_FAMILY_EXTERNAL;
        barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    template<class Ready> void waitUntil(Ready ready,const GpuFrameCompositor::CancellationPredicate& cancel) {
        const auto start=Clock::now();
        struct Elapsed {std::uint64_t& total;Clock::time_point start;~Elapsed(){total+=ns(start);}} elapsed{metrics.wait_nanoseconds,start};
        while(!ready()){
            cancelled(cancel);
            if(ns(start)>10'000'000'000ULL)throw Failure("The native export consumer timed out.",VK_TIMEOUT);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void finish(Slot& slot,const GpuFrameCompositor::CancellationPredicate& cancel) {
        auto& p=*device->impl_;
        {
            std::unique_lock lock(p.mutex);p.reap();
            const auto start=Clock::now();
            if(!slot.completion->done)p.wait(*slot.context,lock,cancel);
            metrics.wait_nanoseconds+=ns(start);
        }
        if(slot.ready)return;
        if(!slot.acquired){
            waitUntil([&]{const auto hr=slot.imported->mutex->AcquireSync(1,0);
                if(hr==WAIT_TIMEOUT)return false;
                if(hr!=S_OK)throw Failure("Acquire native export keyed mutex.",hr);
                slot.acquired=true;return true;},cancel);
        }
        if(!slot.copy_issued){
            slot.destination->with_device_lock([&]{
                commands->CopyResource(static_cast<ID3D11Resource*>(slot.destination->d3d11_view().texture),slot.imported->texture.Get());
                commands->End(slot.copy_done.Get());commands->Flush();
            });
            slot.copy_issued=true;metrics.d3d_copy_bytes+=std::uint64_t(width)*height*4;
        }
        waitUntil([&]{HRESULT result=S_FALSE;
            slot.destination->with_device_lock([&]{result=commands->GetData(slot.copy_done.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);});
            checkHr(result,"Collect D3D11 encoder copy.");return result==S_OK;},cancel);
        checkHr(slot.imported->mutex->ReleaseSync(0),"Release native export keyed mutex.");
        slot.acquired=false;slot.ready=true;
    }
#endif
    explicit Impl(std::shared_ptr<VulkanDevice> value,std::shared_ptr<void> reservation):device(std::move(value)),external_reservation(std::move(reservation)){}
};
VulkanNativeExportBridge::VulkanNativeExportBridge(std::shared_ptr<VulkanDevice> device):VulkanNativeExportBridge(std::move(device),{}){}
VulkanNativeExportBridge::VulkanNativeExportBridge(std::shared_ptr<VulkanDevice> device,std::shared_ptr<void> reservation):impl_(std::make_unique<Impl>(std::move(device),std::move(reservation))){}
VulkanNativeExportBridge::~VulkanNativeExportBridge(){
    discard();if(!isRetired()){
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan-native-export","retain-unconfirmed-resources",
            "Native retirement was unconfirmed. Bounded GPU resources and their reservation remain held until process exit.");
        // Deliberate bounded quarantine: freeing cross-API memory still consumed
        // by a hung device is unsafe. No destructor runs after QGuiApplication.
        static_cast<void>(impl_.release());
    }
}
std::optional<std::array<std::uint8_t,8>> VulkanNativeExportBridge::adapterLuid(
    const media::NativeVideoFramePtr& frame,std::string& cause) {
#ifdef CS_VULKAN_NATIVE_EXPORT
    try {checkNativeDestination(frame);return adapterIdentity(static_cast<ID3D11Device*>(frame->d3d11_view().device));}
    catch(const std::exception& error){cause=error.what();return {};}
#else
    Q_UNUSED(frame);cause="The Vulkan/D3D11 bridge is unavailable in this build.";return {};
#endif
}
GpuCompositionResult VulkanNativeExportBridge::prepare(const media::NativeVideoFramePtr& frame,unsigned maximum,bool reject_format) {
    discard();
    if(!isRetired())return {Status::Failed,{},"prepare-native-export","Previous native resources have not retired safely."};
#ifdef CS_VULKAN_NATIVE_EXPORT
    impl_->native_slots.clear();
#endif
    try {
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;
        if(!b.device || !b.device->valid() || !b.device->impl_->native_export)
            throw Failure("Native Vulkan export was not explicitly enabled.",0,Status::Unsupported);
        checkNativeDestination(frame);const auto view=frame->d3d11_view();
        b.native_slots.clear();b.failed=false;b.d3d=static_cast<ID3D11Device*>(view.device);b.d3d->GetImmediateContext(&b.commands);
        auto& p=*b.device->impl_;std::lock_guard lock(p.mutex);
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
        vkGetPhysicalDeviceProperties2(p.physical,&properties);const auto luid=adapterIdentity(b.d3d.Get(),true);
        if(!id.deviceLUIDValid || std::memcmp(id.deviceLUID,luid.data(),luid.size()))
            throw Failure("Vulkan and NVENC use different adapters.",0,Status::Unsupported);
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
        external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};query.pNext=&external;
        query.format=VK_FORMAT_B8G8R8A8_UNORM;query.type=VK_IMAGE_TYPE_2D;query.tiling=VK_IMAGE_TILING_OPTIMAL;
        query.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkExternalImageFormatProperties external_properties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 format{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};format.pNext=&external_properties;
        const auto status=vkGetPhysicalDeviceImageFormatProperties2(p.physical,&query,&format);
        VkFormatProperties src{},dst{};vkGetPhysicalDeviceFormatProperties(p.physical,VK_FORMAT_R8G8B8A8_UNORM,&src);
        vkGetPhysicalDeviceFormatProperties(p.physical,VK_FORMAT_B8G8R8A8_UNORM,&dst);
        if(reject_format || status==VK_ERROR_FORMAT_NOT_SUPPORTED || !(external_properties.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
            !(src.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !(dst.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
            throw Failure("The required shared BGRA8 import/blit format is unsupported.",0,Status::Unsupported);
        check(status,"Query native export image format.");
        if(!maximum || maximum>2 || view.width<=0 || view.height<=0 || unsigned(view.width)>format.imageFormatProperties.maxExtent.width || unsigned(view.height)>format.imageFormatProperties.maxExtent.height)
            throw Failure("Invalid bounded native export dimensions or slot count.",0,Status::Unsupported);
        b.width=view.width;b.height=view.height;
        const auto bytes=std::uint64_t(b.width)*b.height*4;
        // Include source, UINT scratch, normalized scratch and shared image for
        // each flight, plus the externally reserved native encoder pool.
        while(maximum>1 && bytes*4*maximum>p.capacity)--maximum;
        if(bytes*4>p.capacity)throw Failure("No native export flight fits the known memory budget.",0,Status::Busy);
        auto get_properties=reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vkGetDeviceProcAddr(p.device,"vkGetMemoryWin32HandlePropertiesKHR"));
        if(!get_properties)throw Failure("The native memory import entry point is unavailable.",0,Status::Unsupported);
        for(unsigned index=0;index<maximum;++index){
            auto imported=std::make_shared<Impl::Imported>();imported->owner=&p;
            D3D11_TEXTURE2D_DESC texture{};texture.Width=b.width;texture.Height=b.height;texture.MipLevels=1;texture.ArraySize=1;
            texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
            texture.BindFlags=0;
            texture.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            frame->with_device_lock([&]{checkHr(b.d3d->CreateTexture2D(&texture,nullptr,&imported->texture),"Create shared encoder bridge texture.");});
            checkHr(imported->texture.As(&imported->mutex),"Query shared encoder mutex.");
            ComPtr<IDXGIResource1> resource;checkHr(imported->texture.As(&resource),"Query shared encoder resource.");
            struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}} handle;
            checkHr(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"Create encoder NT handle.");
            VkExternalMemoryImageCreateInfo image_external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};image_external.handleTypes=external.handleType;
            VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.pNext=&image_external;image.imageType=VK_IMAGE_TYPE_2D;
            image.format=query.format;image.extent={unsigned(b.width),unsigned(b.height),1};image.mipLevels=1;image.arrayLayers=1;
            image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=query.tiling;image.usage=query.usage;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateImage(p.device,&image,nullptr,&imported->image),"Create imported encoder image.");
            VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(p.device,imported->image,&requirements);
            p.trim(requirements.size);
            if(p.bytes+requirements.size>p.capacity){if(index)break;throw Failure("The shared encoder import exceeds the memory budget.",0,Status::Busy);}
            VkMemoryWin32HandlePropertiesKHR bits{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
            check(get_properties(p.device,external.handleType,handle.value,&bits),"Query shared encoder memory type.");
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=imported->image;
            VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};import.pNext=&dedicated;
            import.handleType=external.handleType;import.handle=handle.value;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&import;allocation.allocationSize=requirements.size;
            allocation.memoryTypeIndex=p.memoryType(requirements.memoryTypeBits&bits.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(p.device,&allocation,nullptr,&imported->memory),"Import shared encoder allocation.");
            imported->bytes=requirements.size;p.bytes+=requirements.size;p.peak=std::max(p.peak,p.bytes);p.known_peak=std::max(p.known_peak,p.bytes+p.external_reserved);
            check(vkBindImageMemory(p.device,imported->image,imported->memory,0),"Bind shared encoder image.");
            Impl::Slot slot;slot.imported=std::move(imported);
            D3D11_QUERY_DESC event{D3D11_QUERY_EVENT,0};checkHr(b.d3d->CreateQuery(&event,&slot.copy_done),"Create encoder copy completion.");
            b.native_slots.push_back(std::move(slot));
        }
        return {Status::Complete};
#else
        Q_UNUSED(frame);Q_UNUSED(maximum);Q_UNUSED(reject_format);return {Status::Unsupported,{},"native-export","The Vulkan/D3D11 bridge is unavailable in this build."};
#endif
    }catch(const Failure& error){
#ifdef CS_VULKAN_NATIVE_EXPORT
        impl_->native_slots.clear();
#endif
        return {error.status,{},"prepare-native-export",error.what(),error.code};}
    catch(const std::exception& error){
#ifdef CS_VULKAN_NATIVE_EXPORT
        impl_->native_slots.clear();
#endif
        return {Status::Failed,{},"prepare-native-export",error.what()};}
}
VulkanNativeExportResult VulkanNativeExportBridge::submit(const GpuTextureFramePtr& source,
    const media::NativeVideoFramePtr& destination,const GpuFrameCompositor::CancellationPredicate& cancel) {
#ifdef CS_VULKAN_NATIVE_EXPORT
    VulkanDevice::Impl::Context* context=nullptr;
#endif
    try {
        cancelled(cancel);
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;if(b.failed)throw Failure("The native bridge failed; create fresh resources.");
        if(b.native_slots.empty())throw Failure("The native bridge was not prepared.",0,Status::Unsupported);
        checkNativeDestination(destination);
        if(!source || !source->vulkanImage() || source->vulkanImage()->device!=b.device || source->width()!=b.width || source->height()!=b.height ||
            destination->width()!=b.width || destination->height()!=b.height || destination->d3d11_view().device!=b.d3d.Get())
            throw Failure("The native export source or destination does not match the bridge.",0,Status::Unsupported);
        auto slot=std::find_if(b.native_slots.begin(),b.native_slots.end(),[](const auto& item){return !item.ticket;});
        if(slot==b.native_slots.end())return {Status::Busy};
        if(std::any_of(b.native_slots.begin(),b.native_slots.end(),[&](const auto& item){return item.ticket && item.destination==destination;}))return {Status::Busy};
        auto& p=*b.device->impl_;std::lock_guard lock(p.mutex);context=p.current();if(!context)context=p.start(true);if(!context)return {Status::Busy};
        auto scratch=p.image(b.device,b.width,b.height);
        auto display=p.image(b.device,b.width,b.height,VK_FORMAT_R8G8B8A8_UNORM);
        Parameters parameters;parameters.control={9,0,0,0};p.dispatch(source->vulkanImage(),{},{},scratch,parameters,display);
        p.current()->resources.push_back(slot->imported);
        auto cmd=p.begin();b.ownership(cmd,slot->imported->image,true);
        auto& allocation=p.allocation(display);p.barrier(cmd,allocation.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        region.srcOffsets[1]=region.dstOffsets[1]={b.width,b.height,1};
        vkCmdBlitImage(cmd,allocation.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,slot->imported->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
        p.barrier(cmd,allocation.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);b.ownership(cmd,slot->imported->image,false);
        const uint64_t acquire_key=0,release_key=1;const uint32_t timeout=10'000;
        VkWin32KeyedMutexAcquireReleaseInfoKHR sync{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};
        sync.acquireCount=1;sync.pAcquireSyncs=&slot->imported->memory;sync.pAcquireKeys=&acquire_key;sync.pAcquireTimeouts=&timeout;
        sync.releaseCount=1;sync.pReleaseSyncs=&slot->imported->memory;sync.pReleaseKeys=&release_key;
        const auto submitted_at=Clock::now();
        slot->completion=p.finish(context,VK_NULL_HANDLE,VK_NULL_HANDLE,&sync);b.metrics.submit_nanoseconds+=ns(submitted_at);slot->context=context;context=nullptr;
        static std::atomic_uint64_t next{1};slot->ticket=next.fetch_add(1);slot->source=source;slot->destination=destination;
        b.order.push_back(slot->ticket);++b.metrics.submissions;b.metrics.peak_pending=std::max(b.metrics.peak_pending,unsigned(b.order.size()));
        b.metrics.vulkan_blit_bytes+=std::uint64_t(b.width)*b.height*4;
        return {Status::Complete,{slot->ticket}};
#else
        Q_UNUSED(source);Q_UNUSED(destination);return {Status::Unsupported,{}, {},"The Vulkan/D3D11 bridge is unavailable in this build."};
#endif
    }catch(const Failure& error){
#ifdef CS_VULKAN_NATIVE_EXPORT
        if(context)try{std::lock_guard lock(impl_->device->impl_->mutex);impl_->device->impl_->finish(context);}catch(...){impl_->failed=true;}
        if(error.status==Status::Failed)impl_->failed=true;
#endif
        return {error.status,{}, {},error.what(),error.code};
    }catch(const std::exception& error){
#ifdef CS_VULKAN_NATIVE_EXPORT
        if(context)try{std::lock_guard lock(impl_->device->impl_->mutex);impl_->device->impl_->finish(context);}catch(...){impl_->device->impl_->lost=true;}
#endif
        impl_->failed=true;return {Status::Failed,{}, {},error.what()};}
}
VulkanNativeExportResult VulkanNativeExportBridge::collect(VulkanNativeExportTicket ticket,const GpuFrameCompositor::CancellationPredicate& cancel) {
    try {
        cancelled(cancel);
#ifdef CS_VULKAN_NATIVE_EXPORT
        auto& b=*impl_;if(b.retirement_failed)return {Status::Failed,{}, {},"Native retirement is unconfirmed."};
        auto slot=std::find_if(b.native_slots.begin(),b.native_slots.end(),[&](const auto& value){return value.ticket && value.ticket==ticket.value;});
        if(slot==b.native_slots.end())return {Status::Unsupported,{}, {},"Unknown or retired native export ticket."};
        if(b.order.front()!=ticket.value)return {Status::Busy,{}, {},"Native export tickets must be collected FIFO."};
        b.finish(*slot,cancel);cancelled(cancel);
        auto frame=std::move(slot->destination);slot->source.reset();slot->completion.reset();slot->context=nullptr;
        slot->ticket=0;slot->acquired=slot->copy_issued=slot->ready=false;b.order.pop_front();++b.metrics.collections;
        return {Status::Complete,ticket,std::move(frame)};
#else
        Q_UNUSED(ticket);return {Status::Unsupported,{}, {},"The Vulkan/D3D11 bridge is unavailable in this build."};
#endif
    }catch(const Failure& error){if(error.status==Status::Failed)impl_->failed=true;return {error.status,{}, {},error.what(),error.code};}
    catch(const std::exception& error){impl_->failed=true;return {Status::Failed,{}, {},error.what()};}
}
void VulkanNativeExportBridge::discard() noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    auto& b=*impl_;if(b.retirement_failed)return;
    for(auto& slot:b.native_slots)if(slot.ticket){
        try{
            if(qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST")==1)throw Failure("Injected unconfirmed native retirement.",VK_TIMEOUT);
            b.finish(slot,{});
        }catch(const std::exception& error){b.failed=true;b.retirement_failed=true;
            const auto* failure=dynamic_cast<const Failure*>(&error);
            b.metrics.retirement_error_code=failure?failure->code:0;
            try{creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan-native-export","discard",error.what(),
                {{"error_code",std::to_string(b.metrics.retirement_error_code)},{"ticket",std::to_string(slot.ticket)}});}catch(...){
                creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan-native-export","discard",error.what());}
            continue;}
        slot.source.reset();slot.destination.reset();slot.completion.reset();slot.context=nullptr;slot.ticket=0;
        slot.acquired=slot.copy_issued=slot.ready=false;
    }
    if(!b.retirement_failed)b.order.clear();
#endif
}
bool VulkanNativeExportBridge::isRetired() const noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    return std::none_of(impl_->native_slots.begin(),impl_->native_slots.end(),[](const auto& slot){return slot.ticket!=0;});
#else
    return true;
#endif
}
unsigned VulkanNativeExportBridge::slotCount() const noexcept {
#ifdef CS_VULKAN_NATIVE_EXPORT
    return unsigned(impl_->native_slots.size());
#else
    return 0;
#endif
}
VulkanNativeExportBridge::Metrics VulkanNativeExportBridge::metrics() const noexcept {return impl_->metrics;}
} // namespace creative_suite::composition
