#pragma once
#include <cstdint>
#include <cstddef>
#include <span>                // C++20: ShaderDesc::bytecode
#include "RHI/PipelineTypes.h" // Format, ShaderStage, CompareFunc

// GPU 리소스 기술(Desc). D3D 헤더를 include하지 않음.
//
// 모든 리소스 생성은 "Desc를 넘기면 핸들이 돌아온다"는 한 가지 형태를 따름.
// 예전에는 CreateConstBuffer / CreateVertexBuffer / CreateIndexBuffer 가
// 각각 D3D 버퍼 스마트 포인터를 돌려줬음. 세 함수의 차이는 BindFlags 하나뿐이었음.
//
// 이름과 필드는 RHI 최종형(7단계)과 같게 지었으므로, 7단계에서 인터페이스를 추출하고
// D3D12 백엔드를 추가할 때도 호출 코드는 바뀌지 않았음.

enum class BufferUsage : uint8_t
{
    Default, // GPU 읽기/쓰기. CPU 갱신은 UpdateSubresource(전체 덮어쓰기).
    Dynamic, // CPU가 매 프레임 씀. Map(WRITE_DISCARD). 프레임 수만큼 내부 복제.
    Staging, // CPU 읽기용 사본. 지금은 만들기만 하고 쓰는 곳이 없음.
};

// 비트 플래그. 한 버퍼가 Vertex | ShaderResource 처럼 두 역할을 겸할 수 있음.
enum BufferBind : uint8_t
{
    BufferBind_Vertex = 1 << 0,
    BufferBind_Index = 1 << 1,
    BufferBind_Constant = 1 << 2,
    BufferBind_ShaderResource = 1 << 3, // StructuredBuffer. stride 필요.
};

struct BufferDesc
{
    uint32_t size = 0; // 바이트 단위. 상수버퍼는 16의 배수로 올림 처리됨.
    BufferUsage usage = BufferUsage::Default;
    uint8_t bindFlags = 0;           // BufferBind_* 조합
    uint32_t stride = 0;             // 정점 버퍼: 정점 크기. 구조화 버퍼: 원소 크기.
    const char* debugName = nullptr; // Debug Layer 메시지에 나올 이름. 생성 시에만 읽음.
};

enum TextureBind : uint8_t
{
    TextureBind_RenderTarget = 1 << 0,
    TextureBind_DepthStencil = 1 << 1,
    TextureBind_ShaderResource = 1 << 2,
};

// 2026-10-08: 큐브맵. 큐브는 "면 6장짜리 2D 텍스처 배열 + 큐브로 읽는 뷰"임 — D3D11 은 ArraySize 6 + MISC_TEXTURECUBE,
// D3D12 는 DepthOrArraySize 6 + SRV_DIMENSION_TEXTURECUBE. 면 순서는 D3D 관례 +X, −X, +Y, −Y, +Z, −Z (DDS 파일 순서와 같음).
// 지금 큐브는 ShaderResource 전용임 (렌더 타깃으로 그리는 큐브 — 반사 프로브 — 는 면별 RTV 가 필요해 아직 없음).
enum class TextureDimension : uint8_t
{
    Texture2D,   // 배열 크기 1
    TextureCube, // 배열 크기 6. width == height 여야 함
};

struct TextureDesc
{
    TextureDimension dimension =
        TextureDimension::Texture2D; // D3D12_RESOURCE_DESC 처럼 첫 필드. 생략하면 2D
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::
        Unknown; // sRGB 텍스처는 *_UNORM_SRGB 로 만듦. 샘플링할 때 하드웨어가 선형 값으로 변환함.
    uint32_t mipLevels = 1;
    uint8_t bindFlags = 0; // TextureBind_* 조합
    uint32_t sampleCount = 1;
    const char* debugName = nullptr;
};

// 배열 크기(면 수). 큐브 = 6, 그 외 = 1. 백엔드의 ArraySize / DepthOrArraySize 와 초기 데이터 개수가 이 값을 씀.
constexpr uint32_t GetArraySize(const TextureDesc& desc)
{
    return desc.dimension == TextureDimension::TextureCube ? 6u : 1u;
}

// 텍스처 초기 데이터. 서브리소스(면 × 밉)마다 하나. 밉맵은 CPU에서 만들어 함께 올림.
// 순서는 D3D 서브리소스 인덱스 = mip + face × mipLevels 와 같음: 면 0 의 밉 0..N−1, 면 1 의 밉 0..N−1, ...
// 2D 텍스처는 면이 하나뿐이라 "밉 레벨마다 하나"였던 이전 규칙과 같음.
// D3D12에서는 이 배열이 업로드 힙 → CopyTextureRegion 경로로 처리됨. 호출 코드는 같음.
struct TextureSubresource
{
    const void* data = nullptr;
    uint32_t rowPitch = 0; // 한 행의 바이트 수
};

enum class SamplerFilter : uint8_t
{
    Point,       // 최근접. 밉맵도 최근접
    Linear,      // 3선형 (밉 사이도 보간)
    Anisotropic, // 비등방. maxAnisotropy 사용
    Comparison,  // 그림자 비교 샘플러 (6단계). PCF용 선형 + compareFunc
};

enum class SamplerAddress : uint8_t
{
    Wrap,
    Clamp,
    Mirror,
    Border,
};

struct SamplerDesc
{
    SamplerFilter filter = SamplerFilter::Linear;
    SamplerAddress addressU = SamplerAddress::Wrap;
    SamplerAddress addressV = SamplerAddress::Wrap;
    SamplerAddress addressW = SamplerAddress::Wrap;
    uint8_t maxAnisotropy = 16;
    CompareFunc compareFunc = CompareFunc::LessEqual; // Comparison 필터에서만
    float minLod = 0.0f;
    float maxLod = 1000.0f; // 밉맵을 끄려면 0
    float borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const char* debugName = nullptr;
};

struct ShaderDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    // 컴파일된 바이트코드. 컴파일은 ShaderCompiler가 담당함.
    // C++20 (2026-09-28): (const void*, size_t) 쌍을 std::span 하나로 합침. std::vector<uint8_t> 를 그대로
    // 대입할 수 있고, 포인터와 길이가 따로 다니다 어긋나는 일이 없음. span 은 소유하지 않으므로
    // CreateShader 가 돌아오기 전까지만 원본이 살아 있으면 됨 (백엔드는 사본을 뜸).
    std::span<const uint8_t> bytecode;
    const char* debugName = nullptr;
};

// 상수버퍼를 어느 스테이지에 바인딩할지 지정함. D3D11은 스테이지마다 슬롯이 따로 있음.
enum ShaderStageMask : uint8_t
{
    ShaderStageMask_Vertex = 1 << 0,
    ShaderStageMask_Pixel = 1 << 1,
};
