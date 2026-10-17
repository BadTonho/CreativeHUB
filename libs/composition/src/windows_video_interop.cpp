#include "windows_video_interop.h"
#include "windows_nv12_converter.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

namespace creative_suite::composition::detail {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
using Microsoft::WRL::ComPtr;
namespace {
OpenGlCompositionResult failed(const char* operation, std::string cause, HRESULT code) {
    return {OpenGlCompositionStatus::Failed, {}, operation, std::move(cause), code};
}
}
struct WindowsVideoInterop::Impl : WindowsNv12Converter {
    QOpenGLContext* context;
    QOpenGLFunctions_3_2_Core* gl;
    using OpenDevice = HANDLE(WINAPI*)(void*);
    using CloseDevice = BOOL(WINAPI*)(HANDLE);
    using Register = HANDLE(WINAPI*)(HANDLE, void*, GLuint, GLenum, GLenum);
    using Unregister = BOOL(WINAPI*)(HANDLE, HANDLE);
    using Lock = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);
    OpenDevice open_device = nullptr;
    CloseDevice close_device = nullptr;
    Register register_object = nullptr;
    Unregister unregister_object = nullptr;
    Lock lock_objects = nullptr, unlock_objects = nullptr;
    HANDLE interop_device = nullptr, object = nullptr;
    GLuint texture = 0;
    bool locked = false;
    bool available = false;
    struct ExportTarget {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11Texture2D> texture;
        HANDLE connection = nullptr, object = nullptr;
        GLuint name = 0;
    };
    std::vector<ExportTarget> export_targets;
    Impl(QOpenGLContext* current, QOpenGLFunctions_3_2_Core* functions) : context(current), gl(functions) {
        using Extensions = const char*(WINAPI*)(HDC);
        const auto extensions = reinterpret_cast<Extensions>(context->getProcAddress("wglGetExtensionsStringARB"));
        const char* names = extensions ? extensions(wglGetCurrentDC()) : nullptr;
        available = names && std::strstr(names, "WGL_NV_DX_interop2");
        open_device = reinterpret_cast<OpenDevice>(context->getProcAddress("wglDXOpenDeviceNV"));
        close_device = reinterpret_cast<CloseDevice>(context->getProcAddress("wglDXCloseDeviceNV"));
        register_object = reinterpret_cast<Register>(context->getProcAddress("wglDXRegisterObjectNV"));
        unregister_object = reinterpret_cast<Unregister>(context->getProcAddress("wglDXUnregisterObjectNV"));
        lock_objects = reinterpret_cast<Lock>(context->getProcAddress("wglDXLockObjectsNV"));
        unlock_objects = reinterpret_cast<Lock>(context->getProcAddress("wglDXUnlockObjectsNV"));
        available = available && open_device && close_device && register_object && unregister_object && lock_objects && unlock_objects;
    }
    void clear() {
        if (locked && object) { unlock_objects(interop_device, 1, &object); locked = false; }
        if (object) { unregister_object(interop_device, object); object = nullptr; }
        if (texture) { gl->glDeleteTextures(1, &texture); texture = 0; }
        if (interop_device) { close_device(interop_device); interop_device = nullptr; }
        target.Reset(); y_view.Reset(); uv_view.Reset(); nv12.Reset(); rgba.Reset();
        vertex.Reset(); pixel.Reset(); constants.Reset(); commands.Reset(); device.Reset();
        width = height = 0;
        storage_width = storage_height = 0;
    }
    ~Impl() {
        for (auto& export_target : export_targets) {
            if (export_target.object) unregister_object(export_target.connection, export_target.object);
            if (export_target.name) gl->glDeleteTextures(1, &export_target.name);
            if (export_target.connection) close_device(export_target.connection);
        }
        clear();
    }
    HRESULT create(const media::D3D11VideoFrameView& view, const D3D11_TEXTURE2D_DESC& source) {
        clear(); device = static_cast<ID3D11Device*>(view.device); device->GetImmediateContext(&commands);
        width = view.width; height = view.height;
        storage_width = source.Width; storage_height = source.Height;
        interop_device = open_device(device.Get());
        if (!interop_device) return HRESULT_FROM_WIN32(GetLastError());
        auto result = WindowsNv12Converter::create(view,source);
        if(FAILED(result))return result;
        gl->glGenTextures(1, &texture); gl->glBindTexture(GL_TEXTURE_2D, texture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        object = register_object(interop_device, rgba.Get(), texture, GL_TEXTURE_2D, 0 /* WGL_ACCESS_READ_ONLY_NV */);
        if (!object) { const auto error = GetLastError(); return error ? HRESULT_FROM_WIN32(error) : E_FAIL; }
        return S_OK;
    }
};
#else
struct WindowsVideoInterop::Impl {};
#endif
WindowsVideoInterop::WindowsVideoInterop(QOpenGLContext* context, QOpenGLFunctions_3_2_Core* gl)
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    : impl_(std::make_unique<Impl>(context, gl)) {}
#else
    : impl_(std::make_unique<Impl>()) { (void)context; (void)gl; }
#endif
WindowsVideoInterop::~WindowsVideoInterop() = default;
OpenGlCompositionResult WindowsVideoInterop::begin(const media::NativeVideoFramePtr& frame) {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    if (!impl_->available) return {OpenGlCompositionStatus::Unsupported, {}, "check-d3d11-interop", "WGL_NV_DX_interop2 is unavailable."};
    if (active_import_ && active_import_->locked)
        return {OpenGlCompositionStatus::Busy, {}, "native-import-pool", "A native import is still in use."};
    active_import_ = nullptr;
    if (!frame) return {OpenGlCompositionStatus::Unsupported, {}, "check-native-format", "A native video frame is required."};
    const auto view = frame->d3d11_view();
    if (view.format != media::NativeVideoFormat::Nv12 || !view.texture || !view.device)
        return {OpenGlCompositionStatus::Unsupported, {}, "check-native-format", "Only native NV12 decoding surfaces are supported."};
    auto* source = static_cast<ID3D11Texture2D*>(view.texture);
    D3D11_TEXTURE2D_DESC description{}; source->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_NV12 || view.width <= 0 || view.height <= 0 ||
        std::uint64_t(description.Width) * description.Height * 3 / 2 + std::uint64_t(view.width) * view.height * 4 > 64ULL * 1024 * 1024)
        return {OpenGlCompositionStatus::Unsupported, {}, "check-interop-limit", "The native format or conversion storage exceeds the 64 MiB bridge limit."};
    auto found = std::find_if(imports_.begin(), imports_.end(), [&](const auto& entry) {
        return entry->device.Get() == view.device && entry->width == view.width && entry->height == view.height &&
            entry->storage_width == description.Width && entry->storage_height == description.Height;
    });
    if (found == imports_.end()) {
        if (imports_.size() == 4) imports_.erase(imports_.begin());
        imports_.push_back(std::make_unique<Impl>(impl_->context, impl_->gl));
    } else {
        // Move the reused bridge to the back: the front is the next eviction.
        auto reused = std::move(*found);
        imports_.erase(found);
        imports_.push_back(std::move(reused));
    }
    active_import_ = imports_.back().get();
    auto& p = *active_import_;
    HRESULT result = S_OK;
    frame->with_device_lock([&] {
        if (p.device.Get() != view.device || p.width != view.width || p.height != view.height ||
            p.storage_width != description.Width || p.storage_height != description.Height || !p.object)
            result = p.create(view, description);
        if (FAILED(result)) return;
        result = p.convert(view);
    });
    if (FAILED(result)) return failed("convert-native-video", "Creating or converting the native video texture failed.", result);
    if (!p.lock_objects(p.interop_device, 1, &p.object))
        return failed("lock-native-video", "Cannot synchronize D3D11 video with OpenGL.", HRESULT_FROM_WIN32(GetLastError()));
    p.locked = true;
    return {OpenGlCompositionStatus::Complete};
#else
    (void)frame;
    return {OpenGlCompositionStatus::Unsupported, {}, "check-d3d11-interop", "D3D11/OpenGL interoperability is available only on Windows."};
#endif
}
OpenGlCompositionResult WindowsVideoInterop::end() {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    if (active_import_ && active_import_->locked) {
        auto& p = *active_import_;
        if (!p.unlock_objects(p.interop_device, 1, &p.object))
            return failed("unlock-native-video", "Cannot release the synchronized native video texture.", HRESULT_FROM_WIN32(GetLastError()));
        p.locked = false;
    }
    active_import_ = nullptr;
#endif
    return {OpenGlCompositionStatus::Complete};
}
OpenGlCompositionResult WindowsVideoInterop::write(unsigned source_texture, const media::NativeVideoFramePtr& destination) {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    auto& p = *impl_;
    if (!p.available) return {OpenGlCompositionStatus::Unsupported, {}, "check-encoding-interop", "WGL_NV_DX_interop2 is unavailable."};
    if (!destination) return {OpenGlCompositionStatus::Unsupported, {}, "check-encoding-frame", "A native encoding frame is required."};
    const auto view = destination->d3d11_view();
    if (view.format != media::NativeVideoFormat::Bgra8 || view.array_slice || !view.texture || !view.device)
        return {OpenGlCompositionStatus::Unsupported, {}, "check-encoding-format", "A single BGRA D3D11 texture is required."};
    auto found = std::find_if(p.export_targets.begin(), p.export_targets.end(), [&](const auto& target) { return target.texture.Get() == view.texture; });
    if (found == p.export_targets.end()) {
        if (p.export_targets.size() >= 4) return {OpenGlCompositionStatus::Busy, {}, "encoding-interop-pool", "The four native encoding registrations are occupied."};
        p.export_targets.emplace_back(); found = std::prev(p.export_targets.end());
        found->device = static_cast<ID3D11Device*>(view.device);
        found->texture = static_cast<ID3D11Texture2D*>(view.texture);
        found->connection = p.open_device(view.device);
        if (!found->connection) return failed("open-encoding-interop", "Cannot open the encoding device for OpenGL.", HRESULT_FROM_WIN32(GetLastError()));
        p.gl->glGenTextures(1, &found->name);
        found->object = p.register_object(found->connection, view.texture, found->name, GL_TEXTURE_2D, 2 /* WGL_ACCESS_WRITE_DISCARD_NV */);
        if (!found->object) return failed("register-encoding-texture", "Cannot register the encoding texture with OpenGL.", HRESULT_FROM_WIN32(GetLastError()));
    }
    auto& target = *found;
    if (!target.object || !p.lock_objects(target.connection, 1, &target.object))
        return failed("lock-encoding-texture", "Cannot synchronize OpenGL with the encoding texture.", HRESULT_FROM_WIN32(GetLastError()));
    GLuint buffers[2]{}; p.gl->glGenFramebuffers(2, buffers);
    p.gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, buffers[0]);
    p.gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source_texture, 0);
    p.gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffers[1]);
    p.gl->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.name, 0);
    const bool complete = p.gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        p.gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) p.gl->glBlitFramebuffer(0, 0, view.width, view.height, 0, view.height, view.width, 0,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p.gl->glBindFramebuffer(GL_FRAMEBUFFER, 0); p.gl->glDeleteFramebuffers(2, buffers);
    const auto error = p.gl->glGetError();
    const bool unlocked = p.unlock_objects(target.connection, 1, &target.object);
    if (!complete || error || !unlocked) return {OpenGlCompositionStatus::Failed, {}, "copy-encoding-texture",
        "Copying or releasing the native encoding texture failed.", error ? error : GetLastError()};
    return {OpenGlCompositionStatus::Complete};
#else
    (void)source_texture; (void)destination;
    return {OpenGlCompositionStatus::Unsupported, {}, "check-encoding-interop", "Native D3D11 encoding is available only on Windows."};
#endif
}
unsigned WindowsVideoInterop::texture() const noexcept {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    return active_import_ ? active_import_->texture : 0;
#else
    return 0;
#endif
}
std::uint64_t WindowsVideoInterop::reservedBytes() const noexcept {
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
    std::uint64_t bytes = 0;
    for (const auto& entry : imports_)
        bytes += std::uint64_t(entry->storage_width) * entry->storage_height * 3 / 2 +
            std::uint64_t(entry->width) * entry->height * 4;
    return bytes;
#else
    return 0;
#endif
}
}
