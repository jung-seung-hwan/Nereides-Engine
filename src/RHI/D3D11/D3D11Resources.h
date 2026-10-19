#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "RHI/RenderPassTypes.h"
#include "RHI/GraphicsConfig.h"
#include "RHI/Compat/SherlockLog.h"
#include <string>
#include <vector>

// 풀에 들어가는 D3D11 리소스 항목. Device와 CommandList만 직접 다루고, 폴더 밖에서는 핸들만 봄.

struct D3D11Buffer
{
    BufferDesc desc; // debugName은 아래 name으로 복사되고 여기서는 nullptr
    std::string name;
    // Dynamic 버퍼는 프레임 수만큼 복제함(GraphicsConfig.h 참고). 나머지는 [0]만 씀.
    ComPtr<ID3D11Buffer> buffers[kFrameCount];

    ID3D11Buffer* Get(uint32_t frameIndex) const
    {
        const uint32_t slot = (desc.usage == BufferUsage::Dynamic) ? (frameIndex % kFrameCount) : 0;
        return buffers[slot].Get();
    }
};

// 깊이 포맷의 세 가지 형태. 깊이 텍스처를 셰이더에서도 읽으려면(그림자 맵) 리소스는 TYPELESS로
// 만들고 DSV는 D32_FLOAT, SRV는 R32_FLOAT 로 봄. 같은 비트를 두 방식으로 해석하는 것임.
// D3D12도 규칙이 정확히 같음.
namespace D3D11DepthFormat
{
inline bool IsDepth(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_D24_UNORM_S8_UINT;
}
inline DXGI_FORMAT Typeless(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24G8_TYPELESS;
    default:
        return f;
    }
}
inline DXGI_FORMAT ShaderView(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    default:
        return f;
    }
}
}

struct D3D11Texture
{
    TextureDesc desc;
    std::string name;
    ComPtr<ID3D11Texture2D> texture;
    bool typeless = false; // 깊이 + SRV: 리소스가 TYPELESS 로 만들어짐
    ResourceState state =
        ResourceState::Common; // CommandList::Barrier 가 추적 (D3D11 에서는 검사용)

    // 뷰는 처음 요청될 때 만듦. bindFlags에 없는 뷰를 요청하면 nullptr를 돌려줌.
    ComPtr<ID3D11RenderTargetView> rtv; // desc.format 그대로
    ComPtr<ID3D11RenderTargetView>
        rtvSrgb; // *_UNORM_SRGB 뷰. 5단계: 이 뷰로 백버퍼에 쓰면 하드웨어가 선형→sRGB로 인코딩함
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv;

    // srgbView=true 면 같은 텍스처의 sRGB 포맷 뷰를 돌려줌. 스왑체인 백버퍼는 UNORM으로 만들고
    // 씬은 sRGB 뷰에, ImGui는 UNORM 뷰에 그림 (flip 모델에서는 백버퍼 자체를 sRGB로 만들 수 없음).
    ID3D11RenderTargetView* GetRTV(ID3D11Device* device, bool srgbView = false)
    {
        if (!texture || !(desc.bindFlags & TextureBind_RenderTarget))
            return nullptr;
        ComPtr<ID3D11RenderTargetView>& target = srgbView ? rtvSrgb : rtv;
        if (!target)
        {
            HRESULT hr;
            if (srgbView || typeless)
            {
                // TYPELESS 리소스(11단계 씬 뷰)에는 뷰 포맷을 명시해야 함.
                D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
                viewDesc.Format = srgbView ? ToSrgb(D3D11Convert::ToDXGI(desc.format))
                                           : D3D11Convert::ToDXGI(desc.format);
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                hr =
                    device->CreateRenderTargetView(texture.Get(), &viewDesc, target.GetAddressOf());
            }
            else
            {
                hr = device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf());
            }
            if (FAILED(hr))
                Log::Error("RTV 생성 실패 (%s, srgb=%d). %s", name.c_str(), srgbView ? 1 : 0,
                           Log::HrToString(hr).c_str());
        }
        return target.Get();
    }

    ID3D11DepthStencilView* GetDSV(ID3D11Device* device)
    {
        if (!dsv && texture && (desc.bindFlags & TextureBind_DepthStencil))
        {
            HRESULT hr;
            if (typeless)
            {
                // TYPELESS 리소스에는 뷰 포맷을 명시해야 함.
                D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
                viewDesc.Format = D3D11Convert::ToDXGI(desc.format);
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                hr = device->CreateDepthStencilView(texture.Get(), &viewDesc, dsv.GetAddressOf());
            }
            else
            {
                hr = device->CreateDepthStencilView(texture.Get(), nullptr, dsv.GetAddressOf());
            }
            if (FAILED(hr))
                Log::Error("DSV 생성 실패 (%s). %s", name.c_str(), Log::HrToString(hr).c_str());
        }
        return dsv.Get();
    }

    ID3D11ShaderResourceView* GetSRV(ID3D11Device* device)
    {
        if (!srv && texture && (desc.bindFlags & TextureBind_ShaderResource))
        {
            HRESULT hr;
            if (typeless)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
                viewDesc.Format = D3D11DepthFormat::ShaderView(D3D11Convert::ToDXGI(
                    desc.format)); // D32 → R32_FLOAT. 색상 TYPELESS 리소스는 원래 포맷(UNORM) 그대로
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MostDetailedMip = 0;
                viewDesc.Texture2D.MipLevels = desc.mipLevels;
                hr = device->CreateShaderResourceView(texture.Get(), &viewDesc, srv.GetAddressOf());
            }
            else if (desc.dimension == TextureDimension::TextureCube)
            {
                // 2026-10-08: 큐브. nullptr desc 로 만들면 TEXTURE2DARRAY 뷰가 되어 HLSL 의 TextureCube 와 맞지 않음 — 차원을 명시해야 함.
                D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
                viewDesc.Format = D3D11Convert::ToDXGI(desc.format);
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                viewDesc.TextureCube.MostDetailedMip = 0;
                viewDesc.TextureCube.MipLevels = desc.mipLevels;
                hr = device->CreateShaderResourceView(texture.Get(), &viewDesc, srv.GetAddressOf());
            }
            else
            {
                // nullptr desc = 텍스처 포맷 그대로, 모든 밉 레벨. sRGB 포맷이면 샘플링할 때 선형으로 변환됨.
                hr = device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf());
            }
            if (FAILED(hr))
                Log::Error("SRV 생성 실패 (%s). %s", name.c_str(), Log::HrToString(hr).c_str());
        }
        return srv.Get();
    }

private:
    static DXGI_FORMAT ToSrgb(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default:
            return format;
        }
    }
};

struct D3D11Shader
{
    ShaderStage stage = ShaderStage::Vertex;
    std::string name;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    std::vector<uint8_t> bytecode; // 입력 레이아웃 생성(VS)과 리플렉션에 필요
};

struct D3D11Sampler
{
    SamplerDesc desc;
    std::string name;
    ComPtr<ID3D11SamplerState> sampler;
};

// D3D11에는 루트 시그니처도 디스크립터 테이블도 없음. 선언을 그대로 보관해 두면
// CommandList::SetResourceSet 이 슬롯마다 개별 바인딩 호출로 풀어냄. D3D12 백엔드에서는
// 이 자리에 루트 시그니처 객체와 디스크립터 힙 오프셋이 들어옴.
struct D3D11BindingLayout
{
    BindingLayoutDesc desc;
    std::string name;
};

struct D3D11ResourceSet
{
    ResourceSetDesc desc;
    std::string name;
};
