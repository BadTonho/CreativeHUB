#include "windows_nv12_converter.h"
#if defined(_WIN32) && defined(CS_NATIVE_VIDEO_INTEROP)
#include <d3dcompiler.h>
#include <cmath>
namespace creative_suite::composition::detail {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char vertex_source[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
})";
constexpr char pixel_source[] = R"(
Texture2D<float> luma : register(t0);
Texture2D<float2> chroma : register(t1);
cbuffer Conversion : register(b0) { int4 coefficients; int4 range; };
float4 main(float4 position : SV_Position) : SV_Target {
    int2 pixel = int2(position.xy);
    int y = int(round(luma.Load(int3(pixel, 0)) * 255));
    int2 uv = int2(round(chroma.Load(int3(pixel / 2, 0)) * 255)) - 128;
    // Match the baseline swscale planar 8-bit converter: each signed
    // fixed-point term truncates before addition, including both green terms.
    y = ((y - range.y) * 8 * range.x) >> 16;
    int3 rgb = y + int3((uv.y * 8 * coefficients.x) >> 16,
        ((uv.x * 8 * coefficients.z) >> 16) + ((uv.y * 8 * coefficients.w) >> 16),
        (uv.x * 8 * coefficients.y) >> 16);
    return float4(clamp(rgb, 0, 255) / 255.0, 1);
})";
}
HRESULT WindowsNv12Converter::create(const media::D3D11VideoFrameView& view,const D3D11_TEXTURE2D_DESC& source,bool shared) {
    device=static_cast<ID3D11Device*>(view.device); device->GetImmediateContext(&commands);
    width=view.width;height=view.height;storage_width=source.Width;storage_height=source.Height;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = source.Width; description.Height = source.Height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = DXGI_FORMAT_NV12; description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT; description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        auto result = device->CreateTexture2D(&description, nullptr, &nv12); if (FAILED(result)) return result;
        D3D11_SHADER_RESOURCE_VIEW_DESC shader_view{};
        shader_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; shader_view.Texture2D.MipLevels = 1;
        shader_view.Format = DXGI_FORMAT_R8_UNORM;
        result = device->CreateShaderResourceView(nv12.Get(), &shader_view, &y_view); if (FAILED(result)) return result;
        shader_view.Format = DXGI_FORMAT_R8G8_UNORM;
        result = device->CreateShaderResourceView(nv12.Get(), &shader_view, &uv_view); if (FAILED(result)) return result;
        description.Width = width; description.Height = height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if(shared)description.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        result = device->CreateTexture2D(&description, nullptr, &rgba); if (FAILED(result)) return result;
        result = device->CreateRenderTargetView(rgba.Get(), nullptr, &target); if (FAILED(result)) return result;
        ComPtr<ID3DBlob> code, errors;
        result = D3DCompile(vertex_source, sizeof(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors);
        if (FAILED(result)) return result;
        result = device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex); if (FAILED(result)) return result;
        code.Reset(); errors.Reset();
        result = D3DCompile(pixel_source, sizeof(pixel_source), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
        if (FAILED(result)) return result;
        result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel); if (FAILED(result)) return result;
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 32;
        buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = device->CreateBuffer(&buffer, nullptr, &constants); if (FAILED(result)) return result;
    return S_OK;
}
HRESULT WindowsNv12Converter::convert(const media::D3D11VideoFrameView& view) {
    auto* source=static_cast<ID3D11Texture2D*>(view.texture);
    D3D11_TEXTURE2D_DESC description{};source->GetDesc(&description);
        commands->ClearState();
        commands->CopySubresourceRegion(nv12.Get(), 0, 0, 0, 0, source,
            D3D11CalcSubresource(0, view.array_slice, description.MipLevels), nullptr);
        ID3D11RenderTargetView* render_target = target.Get();
        commands->OMSetRenderTargets(1, &render_target, nullptr);
        D3D11_VIEWPORT viewport{}; viewport.Width = float(view.width); viewport.Height = float(view.height); viewport.MaxDepth = 1;
        commands->RSSetViewports(1, &viewport);
        commands->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->VSSetShader(vertex.Get(), nullptr, 0); commands->PSSetShader(pixel.Get(), nullptr, 0);
        ID3D11ShaderResourceView* planes[]{y_view.Get(), uv_view.Get()};
        commands->PSSetShaderResources(0, 2, planes);
        const double scale = view.full_range ? 224.0 / 255.0 : 1.0;
        int values[8]{};
        for (int i = 0; i < 4; ++i)
            values[i] = static_cast<int>(std::lround(view.yuv_coefficients[i] * scale / 8.0)) * (i >= 2 ? -1 : 1);
        values[4] = view.full_range ? 8192 : 9539;
        values[5] = view.full_range ? 0 : 16;
        commands->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
        ID3D11Buffer* buffer = constants.Get(); commands->PSSetConstantBuffers(0, 1, &buffer);
        commands->Draw(3, 0); commands->ClearState(); commands->Flush();
        return device->GetDeviceRemovedReason();
}
}
#endif
