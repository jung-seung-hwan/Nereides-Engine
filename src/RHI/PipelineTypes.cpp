#include "RHI/Compat/RHICommon.h"
#include "RHI/PipelineTypes.h"
#include <cstring>

// ── 이 파일이 하는 일 ──────────────────────────────────────────────────────
//
//   PipelineStateDesc(= PSO 주문서)의 Hash()와 operator==만 구현함. 저장·조립은 하지 않음.
//   - PSO를 찾고 보관하는 곳: PipelineStateCache (D3D11) / D3D12PipelineStateCache
//   - D3D 상태 객체를 실제로 만드는 곳: PipelineState::Create
//
//	 왜 Desc의 "내용"을 해시하나.
//   렌더러가 드로우 직전에 가진 건 PSO가 아니라 "원하는 설정(Desc)"뿐임. 캐시는
//   "설정 → PSO" 사전이어야 하므로 키는 Desc이고, 해시도 Desc 내용으로 계산해야 함.
//   PSO 객체 포인터를 해시하면 (1) 찾으려면 이미 그 PSO를 쥐고 있어야 하고
//   (2) 같은 설정으로 두 번 만든 PSO가 다른 키가 되어 중복을 막지 못함.
//
//   HashCombine이 하는 일.
//   std::hash는 int·float·포인터 같은 단일 값에만 있고 구조체용은 없음. 그래서 필드를
//   하나씩 순서대로 섞어 숫자 하나(지문)로 접음. HashRasterizer / HashDepthStencil /
//   HashRtBlend는 독립된 "하위 Desc 해시"가 아니라, Hash() 한 줄기 계산을 하위 Desc
//   단위로 떼어 낸 중간 단계임. 앞에서 섞인 h를 받아 이어 섞고 갱신된 h를 돌려줌.
//
//   해시값은 저장하지도, 풀지도 않음.
//   해시는 단방향임. 수백 바이트를 64비트로 접으며 정보가 버려지므로 옵션을 되살릴 수
//   없고, 그럴 필요도 없음. Hash()는 const라 Desc 원본은 그대로 남고, 옵션이 필요한 곳은
//   모두 원본 필드를 직접 읽음:
//     - unordered_map의 키 = Desc 복사본 (해시값이 아님)
//     - PipelineState::Create가 desc.depthStencil.depthEnable 등을 읽어 D3D desc를 채움
//     - PipelineState::m_desc도 원본을 보관
//   해시는 unordered_map이 버킷을 고를 때 한 번 쓰이고 끝남 (도서관의 청구기호 역할).
//   서로 다른 Desc가 같은 해시를 낼 수 있으므로(충돌) 최종 판정은 operator==가 함.
//
//   왜 해시를 쓰나: GetOrCreate는 드로우마다 불림. 저장된 Desc 전부와 == 비교하면 O(N),
//   해시로 버킷을 찾고 1~2개만 비교하면 사실상 O(1). D3D12 PSO 생성은 수~수십 ms라
//   "설정 해시 → PSO 캐시"는 필수 구조임.
//
// ────────────────────────────────────────────────────────────────────────

// 해시는 필드를 하나씩 섞음.
//
// memcpy/memcmp를 쓰지 않는 이유.
// (1) 구조체에는 정렬 패딩이 있고 그 바이트 값은 정해져 있지 않음. 같은 내용의
//     Desc 두 개가 패딩 때문에 다른 해시를 내면 캐시 미스가 계속 남.
// (2) float는 -0.0f == 0.0f 이지만 비트가 다름. 비트 패턴으로 해시하되
//     -0.0f는 0.0f로 정규화해서 넣음.
// (3) 배열은 "쓰는 만큼"만 섞음. rtvCount 뒤의 슬롯과 attributeCount 뒤의
//     속성은 의미가 없으므로 값이 달라도 같은 PSO로 봐야 함.
//
// C++20 (2026-09-28): 하위 Desc 의 비교 함수(EqRasterizer / EqDepthStencil / EqRtBlend /
// EqVertexAttribute / FloatEq)는 삭제하고 헤더의 `operator== = default` 를 씀. 해시 함수는
// 그대로임 — 표준에 "기본 해시"는 없고, (3)의 "쓰는 만큼만" 규칙은 어차피 손으로 써야 함.
// float 비교 의미가 바뀐 지점: 예전 FloatEq 는 비트 비교(±0 만 예외)였고, 지금은 값 비교임.
// 두 방식은 NaN 에서만 다르고(값 비교는 NaN != NaN), 해시와의 일관성(a == b ⇒ hash(a) == hash(b))은
// 값 비교에서도 유지됨 — ±0 은 해시가 정규화하고, 그 외 값이 같으면 비트도 같기 때문.
namespace
{
// 지금까지의 해시(seed)에 값 하나를 섞어 새 해시를 만듦. 순서가 다르면 결과도 다름.
inline uint64_t HashCombine(uint64_t seed, uint64_t value)
{
    // boost::hash_combine의 64비트판. 황금비 상수로 섞음.
    return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
}

// float를 비트 패턴(uint32)으로 바꿔 섞음. 반올림 오차 없이 "같은 값 = 같은 비트"가 됨.
inline uint64_t HashFloat(uint64_t seed, float f)
{
    if (f == 0.0f)
        f = 0.0f; // -0.0f를 +0.0f로
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return HashCombine(seed, bits);
}

// 하위 Desc별 해시. 열거형은 uint8_t로 바꿔 섞고, 필드는 선언 순서대로 넣음.
uint64_t HashRasterizer(uint64_t h, const RasterizerDesc& r)
{
    h = HashCombine(h, static_cast<uint8_t>(r.fill));
    h = HashCombine(h, static_cast<uint8_t>(r.cull));
    h = HashCombine(h, r.frontCounterClockwise);
    h = HashCombine(h, r.depthClipEnable);
    h = HashCombine(h, r.scissorEnable);
    h = HashCombine(h, r.multisampleEnable);
    h = HashCombine(h, r.antialiasedLineEnable);
    h = HashCombine(h, static_cast<uint32_t>(r.depthBias)); // 음수도 비트 그대로 (부호 확장 방지)
    h = HashFloat(h, r.depthBiasClamp);
    h = HashFloat(h, r.slopeScaledDepthBias);
    return h;
}

uint64_t HashDepthStencil(uint64_t h, const DepthStencilDesc& d)
{
    h = HashCombine(h, d.depthEnable);
    h = HashCombine(h, d.depthWrite);
    h = HashCombine(h, static_cast<uint8_t>(d.depthFunc));
    h = HashCombine(h, d.stencilEnable);
    h = HashCombine(h, d.stencilReadMask);
    h = HashCombine(h, d.stencilWriteMask);
    return h;
}

uint64_t HashRtBlend(uint64_t h, const RenderTargetBlendDesc& b)
{
    h = HashCombine(h, b.blendEnable);
    h = HashCombine(h, static_cast<uint8_t>(b.srcColor));
    h = HashCombine(h, static_cast<uint8_t>(b.dstColor));
    h = HashCombine(h, static_cast<uint8_t>(b.colorOp));
    h = HashCombine(h, static_cast<uint8_t>(b.srcAlpha));
    h = HashCombine(h, static_cast<uint8_t>(b.dstAlpha));
    h = HashCombine(h, static_cast<uint8_t>(b.alphaOp));
    h = HashCombine(h, b.writeMask);
    return h;
}

// independentBlend가 꺼져 있으면 D3D는 rt[0]만 봄. 해시도 rt[0]만 반영함.
uint32_t BlendSlotCount(const BlendDesc& b, uint8_t rtvCount)
{
    return b.independentBlend ? rtvCount : 1u;
}
}

// 필드 순서: 셰이더 → 바인딩 레이아웃 → 정점 레이아웃 → 래스터 → 깊이 → 블렌드 → 토폴로지 → 출력 포맷.
// operator==와 같은 필드를 같은 범위로 봐야 함 (a == b ⇒ Hash(a) == Hash(b)).
uint64_t PipelineStateDesc::Hash() const
{
    uint64_t h = 0xCBF29CE484222325ull; // FNV offset basis를 시드로

    // 핸들은 index + generation 둘 다 섞음. 같은 슬롯이 재사용돼도 세대가 다르면 다른 셰이더임.
    h = HashCombine(h, vs.index);
    h = HashCombine(h, vs.generation);
    h = HashCombine(h, ps.index);
    h = HashCombine(h, ps.generation);
    h = HashCombine(h, bindingLayoutCount);
    for (uint32_t i = 0; i < bindingLayoutCount && i < kMaxBindingSets; ++i)
    {
        h = HashCombine(h, bindingLayouts[i].index);
        h = HashCombine(h, bindingLayouts[i].generation);
    }

    // 배열 길이를 먼저 섞음. 그래야 [A]와 [A, B]가 접두사 충돌하지 않음.
    h = HashCombine(h, vertexLayout.attributeCount);
    h = HashCombine(h, vertexLayout.stride);
    for (uint32_t i = 0; i < vertexLayout.attributeCount && i < kMaxVertexAttributes; ++i)
    {
        const VertexAttribute& a = vertexLayout.attributes[i];
        h = HashCombine(h, static_cast<uint8_t>(a.semantic));
        h = HashCombine(h, a.semanticIndex);
        h = HashCombine(h, static_cast<uint8_t>(a.format));
        h = HashCombine(h, a.offset);
        h = HashCombine(h, a.inputSlot);
    }

    h = HashRasterizer(h, rasterizer);
    h = HashDepthStencil(h, depthStencil);

    h = HashCombine(h, blend.alphaToCoverage);
    h = HashCombine(h, blend.independentBlend);
    const uint32_t blendSlots = BlendSlotCount(blend, rtvCount);
    for (uint32_t i = 0; i < blendSlots && i < kMaxRenderTargets; ++i)
    {
        h = HashRtBlend(h, blend.rt[i]);
    }

    h = HashCombine(h, static_cast<uint8_t>(topology));

    h = HashCombine(h, rtvCount);
    for (uint32_t i = 0; i < rtvCount && i < kMaxRenderTargets; ++i)
    {
        h = HashCombine(h, static_cast<uint8_t>(rtvFormats[i]));
    }
    h = HashCombine(h, static_cast<uint8_t>(dsvFormat));
    h = HashCombine(h, sampleCount);

    return h;
}

// 비교 순서는 Hash()와 같음. 개수 필드를 먼저 비교해서 다르면 배열 루프 전에 끝냄.
bool PipelineStateDesc::operator==(const PipelineStateDesc& o) const
{
    // Handle 의 != 는 default <=> 에서, 하위 Desc 의 == / != 는 default == 에서 재작성됨.
    if (vs != o.vs || ps != o.ps)
        return false;
    if (bindingLayoutCount != o.bindingLayoutCount)
        return false;
    for (uint32_t i = 0; i < bindingLayoutCount && i < kMaxBindingSets; ++i)
    {
        if (bindingLayouts[i] != o.bindingLayouts[i])
            return false;
    }

    if (vertexLayout.attributeCount != o.vertexLayout.attributeCount)
        return false;
    if (vertexLayout.stride != o.vertexLayout.stride)
        return false;
    for (uint32_t i = 0; i < vertexLayout.attributeCount && i < kMaxVertexAttributes; ++i)
    {
        if (vertexLayout.attributes[i] != o.vertexLayout.attributes[i])
            return false;
    }

    if (rasterizer != o.rasterizer)
        return false;
    if (depthStencil != o.depthStencil)
        return false;

    if (blend.alphaToCoverage != o.blend.alphaToCoverage)
        return false;
    if (blend.independentBlend != o.blend.independentBlend)
        return false;
    if (rtvCount != o.rtvCount)
        return false; // BlendSlotCount가 rtvCount를 쓰므로 블렌드 루프 전에 확인
    const uint32_t blendSlots = BlendSlotCount(blend, rtvCount);
    for (uint32_t i = 0; i < blendSlots && i < kMaxRenderTargets; ++i)
    {
        if (blend.rt[i] != o.blend.rt[i])
            return false;
    }

    if (topology != o.topology)
        return false;
    for (uint32_t i = 0; i < rtvCount && i < kMaxRenderTargets; ++i)
    {
        if (rtvFormats[i] != o.rtvFormats[i])
            return false;
    }
    return dsvFormat == o.dsvFormat && sampleCount == o.sampleCount;
}
