#pragma once
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <creative_suite/media/native_video_frame.h>
namespace creative_suite::composition::detail {
// Private worker-owned converter. Caller holds NativeVideoFrame's device lock
// for create/convert; output consumption and sharing are owned by the adapter.
struct WindowsNv12Converter {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> commands;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12, rgba;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y_view, uv_view;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    int width=0,height=0;
    unsigned storage_width=0,storage_height=0;
    HRESULT create(const media::D3D11VideoFrameView&, const D3D11_TEXTURE2D_DESC&, bool shared=false);
    HRESULT convert(const media::D3D11VideoFrameView&);
};
}
#endif
