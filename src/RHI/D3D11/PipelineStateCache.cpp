#include "RHI/Compat/RHICommon.h"
#include "RHI/D3D11/PipelineStateCache.h"
#include "RHI/Compat/SherlockLog.h"

// Desc에 맞는 PSO 핸들을 돌려줌. 있으면 재사용, 없으면 만들어서 등록함.
PipelineHandle PipelineStateCache::GetOrCreate(D3D11Device& device, const PipelineStateDesc& desc)
{
    // 캐시 적중: desc.Hash()로 버킷을 찾고 operator==로 확인함 (PipelineTypes.cpp).
    // 저장된 건 인덱스뿐이므로 핸들은 현재 세대를 붙여 새로 만듦.
    const auto found = m_lookup.find(desc);
    if (found != m_lookup.end())
    {
        return PipelineHandle{found->second, m_generation};
    }

    // 캐시 미스: D3D11 상태 객체(래스터라이저·깊이·블렌드·입력 레이아웃)를 실제로 생성함.
    auto state = std::make_unique<PipelineState>();
    if (!state->Create(device, desc))
    {
        // 실패한 Desc는 m_lookup에 넣지 않음 → 다음 요청 때 다시 시도함.
        Log::Error("PipelineStateCache : PSO 생성 실패.");
        return PipelineHandle{}; // 빈 핸들 (IsValid() == false)
    }

    // 인덱스 = m_states 안의 위치. 개별 삭제가 없으므로 Clear() 전까지 바뀌지 않음.
    // unique_ptr로 들고 있어서 vector가 커져 재할당돼도 Get()이 준 포인터는 그대로 유효함.
    const uint32_t index = static_cast<uint32_t>(m_states.size());
    m_states.push_back(std::move(state));
    m_lookup.emplace(desc, index); // Desc는 값으로 복사돼 키가 됨
    Log::Info("PSO 생성 #%u (fill=%s, cull=%d, layouts=%u, gen=%u). 캐시 크기 %zu", index,
              desc.rasterizer.fill == FillMode::Wireframe ? "wire" : "solid",
              static_cast<int>(desc.rasterizer.cull), desc.bindingLayoutCount, m_generation,
              m_states.size());
    return PipelineHandle{index, m_generation};
}

// 핸들 → PSO. 잘못된 핸들은 nullptr.
const PipelineState* PipelineStateCache::Get(PipelineHandle handle) const
{
    // 셋 중 하나라도 걸리면 무효:
    //  - 빈 핸들 (생성 실패했거나 초기화 안 됨)
    //  - 범위 밖 인덱스
    //  - 세대 불일치 = Clear() 이전에 받은 핸들 (셰이더 핫리로드 후 등)
    if (!handle.IsValid() || handle.index >= m_states.size() || handle.generation != m_generation)
    {
        return nullptr;
    }
    return m_states[handle.index].get();
}

// 캐시 전체 폐기. 세대를 올려 기존 핸들을 한꺼번에 무효화함.
void PipelineStateCache::Clear()
{
    if (!m_states.empty())
    {
        Log::Info("PSO 캐시 비움 (%zu개). 이전 세대 %u 의 핸들은 무효.", m_states.size(),
                  m_generation);
    }
    m_lookup.clear();
    m_states.clear();
    ++m_generation;
    if (m_generation == 0)
        m_generation = 1; // uint32 오버플로 시 0(빈 핸들 값)을 건너뜀
}
