#pragma once
#include <cstdint>
#include "RHI/Handle.h"
#include "RHI/PipelineTypes.h" // kMaxRenderTargets

// 렌더 패스와 리소스 상태. D3D 헤더 없음.
//
// RenderPass = "어느 타깃에, 어떤 클리어로 시작하고, 끝날 때 무엇을 남기는가".
// D3D11에서는 OMSetRenderTargets + Clear*View + RSSetViewports 로 풀리고,
// D3D12에서는 같은 호출(OMSetRenderTargets + ClearRenderTargetView)로 풀리거나
// D3D12 커맨드 리스트의 BeginRenderPass 가 받는 BEGINNING/ENDING_ACCESS 로 바뀜.
// Vulkan/Metal의 load/store op 와 같은 개념임. 타일 GPU에서는 DontCare 가
// 실제로 대역폭을 아끼지만 데스크톱 D3D11에서는 힌트일 뿐임.

enum class LoadOp : uint8_t
{
    Load,     // 이전 내용을 유지
    Clear,    // clearColor / clearDepth 로 지움
    DontCare, // 이전 내용이 필요 없음 (전부 덮어쓸 때). D3D11: 아무것도 안 함
};

enum class StoreOp : uint8_t
{
    Store,    // 패스가 끝난 뒤에도 내용이 필요함
    DontCare, // 버려도 됨 (예: MSAA 리졸브 뒤의 원본). D3D11: 아무것도 안 함
};

struct ColorAttachment
{
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    bool srgbView = false;                          // 5단계: UNORM 백버퍼를 sRGB 뷰로 씀
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // 선형 값
};

struct DepthAttachment
{
    TextureHandle texture; // 비어 있으면 깊이 없음 (UI 패스)
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
};

struct Viewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f; // 0 이면 첫 attachment 의 크기를 씀
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct RenderPassDesc
{
    ColorAttachment colors[kMaxRenderTargets];
    uint8_t colorCount = 0;
    DepthAttachment depth;
    Viewport viewport;
    const char* debugName = nullptr;
};

// 리소스 상태 (D3D12_RESOURCE_STATES 의 부분집합).
//
// D3D11은 드라이버가 상태를 추적하므로 Barrier 는 빈 함수임. 그래도 호출을 지금부터
// 넣는 이유: 그림자 맵처럼 "DSV로 쓰고 → SRV로 읽는" 전이가 있는 리소스가 어디서
// 상태를 바꾸는지 코드에 적혀 있어야 D3D12 백엔드가 그 자리에 ResourceBarrier 를 넣을 수 있음.
// D3D11 백엔드는 Debug 빌드에서 before 가 추적 중인 상태와 다르면 경고해 실수를 미리 잡음.
enum class ResourceState : uint8_t
{
    Common,
    RenderTarget,
    DepthWrite,
    ShaderResource,
    CopySource,
    CopyDest,
    Present,
};

inline const char* ToString(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Common:
        return "Common";
    case ResourceState::RenderTarget:
        return "RenderTarget";
    case ResourceState::DepthWrite:
        return "DepthWrite";
    case ResourceState::ShaderResource:
        return "ShaderResource";
    case ResourceState::CopySource:
        return "CopySource";
    case ResourceState::CopyDest:
        return "CopyDest";
    case ResourceState::Present:
        return "Present";
    default:
        return "?";
    }
}
