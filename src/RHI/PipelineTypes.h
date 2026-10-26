#pragma once
#include <cstdint>
#include <cstddef>
#include "RHI/Handle.h"

// API 중립 파이프라인 상태 기술.
//
// 이 헤더는 D3D 헤더를 include하지 않음. 필드 구성은 D3D12의
// D3D12_GRAPHICS_PIPELINE_STATE_DESC를 따름. D3D11에서 쓰지 않는 항목
// (rtvFormats, dsvFormat, sampleCount)도 지금부터 넣어 둠. D3D12 PSO는
// 이 항목 없이는 만들 수 없고, 나중에 추가하면 호출 코드를 전부 바꿔야 함.
//
// 모든 열거형은 uint8_t임. Desc를 해시 키로 쓰므로 크기가 작을수록 좋음.
//
// C++20 (2026-09-28): 하위 Desc(Rasterizer/DepthStencil/RenderTargetBlend/VertexAttribute)의
// operator== 는 `= default` 로 생성함. 멤버를 하나씩 나열한 비교 함수를 PipelineTypes.cpp 에
// 손으로 쓰던 것을 대체함 — 필드를 추가하면 비교에서 빠뜨리는 실수가 없어짐.
// PipelineStateDesc 자체는 default 로 만들 수 없음: 배열을 "쓰는 만큼(rtvCount 등)"만 비교해야 하기 때문.
// != 는 C++20 재작성 규칙으로 == 에서 자동으로 만들어지므로 따로 선언하지 않음.
//
// 호출 코드는 C++20 지정 초기화(designated initializer)로 채움:
//   TextureDesc d{ .width = 2048, .height = 2048, .format = Format::D32_FLOAT, .bindFlags = ... };
// 지정 초기화는 선언 순서를 따라야 하므로, 이 헤더의 필드 순서를 바꾸면 호출 코드가 컴파일되지 않음.
// 그것이 의도임 — D3D12 desc 처럼 "필드 순서가 계약"이 됨.

// 텍스처·정점 속성·인덱스의 데이터 형식. UNORM = 0~255 정수를 0~1 실수로 읽음.
enum class Format : uint8_t
{
    Unknown = 0,         // 미지정 (사용 안 하는 슬롯)
    R8G8B8A8_UNORM,      // 채널당 8비트 RGBA 색 (일반 백버퍼·텍스처)
    R8G8B8A8_UNORM_SRGB, // 위와 같되 읽을 때 sRGB→선형, 쓸 때 선형→sRGB 변환
    D24_UNORM_S8_UINT,   // 깊이 24비트 + 스텐실 8비트
    D32_FLOAT,           // 깊이 32비트 실수 (스텐실 없음, 섀도맵 등)
    R32_FLOAT,           // float 1개
    R32G32_FLOAT,        // float2 (UV 등)
    R32G32B32_FLOAT,     // float3 (위치·노멀 등)
    R32G32B32A32_FLOAT,  // float4 (색·탄젠트 등)
    R16_UINT,            // 16비트 인덱스 (정점 65535개 이하)
    R32_UINT,            // 32비트 인덱스
    R16G16B16A16_FLOAT, // 2026-10-08: half4 색. HDR 큐브맵·BRDF LUT (1.0 을 넘는 값을 담음, 최대 65504)
    R32G32B32A32_UINT,  // Nereides: uint4. 스키닝 뼈 인덱스 (BLENDINDICES)
};

enum class FillMode : uint8_t
{
    Solid,     // 삼각형 내부를 채움
    Wireframe, // 모서리 선만 그림
};

enum class CullMode : uint8_t
{
    None,  // 컬링 안 함 (양면 다 그림)
    Front, // 앞면을 버림
    Back,  // 뒷면을 버림 (기본)
};

// 깊이·스텐실 테스트 비교 함수. "새 값 <op> 기존 값"이 참이면 통과.
enum class CompareFunc : uint8_t
{
    Never,        // 항상 실패
    Less,         // 새 값 < 기존 값
    Equal,        // 새 값 == 기존 값
    LessEqual,    // 새 값 <= 기존 값 (스카이박스 등)
    Greater,      // 새 값 > 기존 값 (Reversed-Z)
    NotEqual,     // 새 값 != 기존 값
    GreaterEqual, // 새 값 >= 기존 값
    Always,       // 항상 통과
};

enum class PrimitiveTopology : uint8_t
{
    TriangleList,  // 정점 3개씩 독립 삼각형
    TriangleStrip, // 이전 2개 정점 + 새 정점 1개로 이어지는 삼각형
    LineList,      // 정점 2개씩 독립 선분
    PointList,     // 정점 하나하나가 점
};

// 블렌딩 계수. 최종 = Src * srcFactor <op> Dst * dstFactor (Src = 셰이더 출력, Dst = 렌더 타깃에 있던 값)
enum class BlendFactor : uint8_t
{
    Zero,        // 0
    One,         // 1
    SrcAlpha,    // 셰이더 출력의 알파
    InvSrcAlpha, // 1 - 셰이더 출력의 알파
    SrcColor,    // 셰이더 출력의 색
    InvSrcColor, // 1 - 셰이더 출력의 색
    DstAlpha,    // 렌더 타깃 알파
    InvDstAlpha, // 1 - 렌더 타깃 알파
};

// 블렌딩에서 두 항을 합치는 연산.
enum class BlendOp : uint8_t
{
    Add,         // Src + Dst
    Subtract,    // Src - Dst
    RevSubtract, // Dst - Src
    Min,         // min(Src, Dst) (계수 무시)
    Max,         // max(Src, Dst) (계수 무시)
};

// 정점 속성의 의미. HLSL 시맨틱 이름(POSITION, NORMAL ...)에 대응함.
enum class VertexSemantic : uint8_t
{
    Position, // POSITION  위치
    Normal,   // NORMAL    법선
    Color,    // COLOR     정점 색
    TexCoord, // TEXCOORD  UV
    Tangent,  // TANGENT   탄젠트 (노멀맵)
    BlendIndices, // BLENDINDICES 스키닝 뼈 인덱스 (Nereides)
    BlendWeight,  // BLENDWEIGHT  스키닝 가중치 (Nereides)
};

enum class ShaderStage : uint8_t
{
    Vertex, // 정점 셰이더
    Pixel,  // 픽셀 셰이더
};

// 기본값은 "불투명, 깊이 테스트 켬, 뒷면 컬링, 솔리드". PipelineStateDesc d{};
// 로 만들고 셰이더·정점 레이아웃만 채우면 일반적인 3D 드로우 설정이 됨.
struct RasterizerDesc
{
    FillMode fill = FillMode::Solid;
    CullMode cull = CullMode::Back;
    bool frontCounterClockwise = false; // false = 시계방향이 앞면 (Luna GeometryGenerator 관례)
    bool depthClipEnable = true;        // near/far 평면 밖 픽셀을 잘라냄
    bool scissorEnable = false;         // 시저 사각형 밖 픽셀을 버림
    bool multisampleEnable = false;     // MSAA 타깃에서 사변형 라인 AA 사용
    bool antialiasedLineEnable = false; // 선 그리기 AA (MSAA 꺼져 있을 때만 의미 있음)
    int32_t depthBias = 0;              // 깊이에 더하는 고정 오프셋 (섀도 아크네 방지)
    float depthBiasClamp = 0.0f;        // 바이어스 최대값 (0 = 제한 없음)
    float slopeScaledDepthBias = 0.0f;  // 경사가 급할수록 커지는 바이어스 배율

    // float 멤버는 값 비교임: -0.0f == 0.0f (해시도 -0.0f 를 0.0f 로 정규화하므로 일관됨).
    // NaN 은 자기 자신과도 다르지만 깊이 바이어스에 NaN 을 넣는 경우는 없음.
    bool operator==(const RasterizerDesc&) const = default;
};

struct DepthStencilDesc
{
    bool depthEnable = true; // 깊이 테스트 수행 여부
    bool depthWrite = true;  // 통과한 픽셀의 깊이를 버퍼에 기록 (반투명은 보통 끔)
    CompareFunc depthFunc = CompareFunc::Less; // 깊이 테스트 비교 함수
    bool stencilEnable = false;                // 스텐실 테스트 수행 여부
    uint8_t stencilReadMask = 0xFF;            // 스텐실 비교 전에 AND 할 마스크
    uint8_t stencilWriteMask = 0xFF;           // 스텐실 기록 시 바꿀 비트
    // 스텐실 연산(StencilOp)은 이를 쓰는 단계가 오면 추가함.

    bool operator==(const DepthStencilDesc&) const = default;
};

struct RenderTargetBlendDesc
{
    // 기본값(One, Zero, Add) = 셰이더 출력으로 덮어씀. 알파 블렌딩은 SrcAlpha, InvSrcAlpha, Add.
    bool blendEnable = false;                 // 꺼져 있으면 아래 계수 무시하고 덮어씀
    BlendFactor srcColor = BlendFactor::One;  // RGB: 셰이더 출력에 곱할 계수
    BlendFactor dstColor = BlendFactor::Zero; // RGB: 렌더 타깃 값에 곱할 계수
    BlendOp colorOp = BlendOp::Add;           // RGB: 두 항을 합치는 연산
    BlendFactor srcAlpha = BlendFactor::One;  // A: 셰이더 출력 계수
    BlendFactor dstAlpha = BlendFactor::Zero; // A: 렌더 타깃 계수
    BlendOp alphaOp = BlendOp::Add;           // A: 연산
    uint8_t writeMask = 0xF;                  // 기록할 채널 비트 (1=R, 2=G, 4=B, 8=A)

    bool operator==(const RenderTargetBlendDesc&) const = default;
};

constexpr uint32_t kMaxRenderTargets = 8;
constexpr uint32_t kMaxVertexAttributes = 8;
constexpr uint32_t kMaxBindingSets =
    4; // 파이프라인 하나가 쓰는 BindingLayout(=ResourceSet) 수 상한

struct BlendDesc
{
    bool alphaToCoverage = false;                // 알파를 MSAA 커버리지 마스크로 변환 (풀·잎 등)
    bool independentBlend = false;               // false면 rt[0] 설정을 모든 RT에 공통 적용
    RenderTargetBlendDesc rt[kMaxRenderTargets]; // 렌더 타깃별 블렌드 설정
};

struct VertexAttribute
{
    VertexSemantic semantic = VertexSemantic::Position; // HLSL 시맨틱 이름
    uint8_t semanticIndex = 0;                          // 시맨틱 뒤 숫자 (TEXCOORD0, TEXCOORD1 ...)
    Format format = Format::Unknown;                    // 데이터 형식 (float3 = R32G32B32_FLOAT)
    uint16_t offset = 0;                                // 정점 구조체 안의 바이트 오프셋 (offsetof)
    uint8_t inputSlot = 0;                              // 정점 버퍼 슬롯 번호 (버퍼 하나면 0)

    bool operator==(const VertexAttribute&) const = default;
};

struct VertexLayoutDesc
{
    VertexAttribute attributes[kMaxVertexAttributes]; // 앞에서 attributeCount개만 유효
    uint8_t attributeCount = 0;                       // 실제 사용 중인 속성 수
    uint16_t stride = 0;                              // 정점 하나의 바이트 크기 (sizeof(Vertex))
};

struct PipelineStateDesc
{
    ShaderHandle vs; // 정점 셰이더
    ShaderHandle ps; // 픽셀 셰이더
    // D3D12의 pRootSignature 자리. 갱신 빈도별 BindingLayout(프레임/오브젝트/재질)을
    // 순서대로 나열한 것이 루트 시그니처 하나에 해당함. D3D11 PSO 생성에는 쓰이지
    // 않지만 "이 파이프라인이 어떤 슬롯을 쓰는가"는 파이프라인의 속성이므로 Desc(해시 키)에 넣음.
    BindingLayoutHandle bindingLayouts[kMaxBindingSets]; // 앞에서 bindingLayoutCount개만 유효
    uint8_t bindingLayoutCount = 0;
    VertexLayoutDesc vertexLayout;                                // 입력 레이아웃 (IA 단계)
    RasterizerDesc rasterizer;                                    // 래스터라이저 단계
    DepthStencilDesc depthStencil;                                // 출력 병합: 깊이·스텐실
    BlendDesc blend;                                              // 출력 병합: 블렌드
    PrimitiveTopology topology = PrimitiveTopology::TriangleList; // 정점을 어떤 도형으로 묶을지

    // D3D11은 이 세 항목을 PSO 생성에 쓰지 않음. D3D12에서는 필수임.
    Format rtvFormats[kMaxRenderTargets] = {Format::R8G8B8A8_UNORM}; // 렌더 타깃별 포맷
    uint8_t rtvCount = 1;                         // 동시에 쓰는 렌더 타깃 수 (MRT)
    Format dsvFormat = Format::D24_UNORM_S8_UINT; // 깊이 버퍼 포맷
    uint8_t sampleCount = 1;                      // MSAA 샘플 수 (1 = 끔)

    // 필드 단위 해시·비교. 구조체를 통째로 memcmp/memcpy하면 패딩 바이트가
    // 섞여 같은 내용이 다른 키가 됨. PipelineTypes.cpp 참고.
    // operator!= 는 선언하지 않음 — C++20 이 == 에서 재작성함.
    uint64_t Hash() const;
    bool operator==(const PipelineStateDesc& other) const;
};

// unordered_map<PipelineStateDesc, ...>의 세 번째 템플릿 인자로 넘기는 해시 함수 객체.
struct PipelineStateDescHasher
{
    size_t operator()(const PipelineStateDesc& desc) const
    {
        return static_cast<size_t>(desc.Hash());
    }
};
