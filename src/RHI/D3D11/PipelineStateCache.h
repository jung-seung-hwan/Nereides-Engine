#pragma once
#include "RHI/D3D11/PipelineState.h"
#include <memory>
#include <unordered_map>
#include <vector>

class D3D11Device;

// Desc → PipelineState 캐시.
//
// 같은 Desc로 두 번 요청하면 같은 핸들이 돌아옴. 호출 코드는 매 프레임
// "지금 설정에 맞는 Desc"를 만들어 GetOrCreate를 불러도 되고, 첫 프레임 이후에는
// 해시 조회 한 번으로 끝남.
//
// PSO는 개별 삭제하지 않음. 대신 Clear()가 전부 버리고 세대를 올림.
// 4단계 셰이더 핫리로드가 이 경로를 씀: 셰이더가 바뀌면 캐시를 비우고,
// 이전 세대의 핸들은 Get()에서 nullptr가 되므로 호출 측이 새로 만들게 됨.
class PipelineStateCache
{
public:
    PipelineHandle GetOrCreate(D3D11Device& device, const PipelineStateDesc& desc);
    const PipelineState* Get(PipelineHandle handle) const;

    size_t Count() const
    {
        return m_states.size();
    }
    uint32_t GetGeneration() const
    {
        return m_generation;
    }
    void Clear();

private:
    std::vector<std::unique_ptr<PipelineState>> m_states;
    std::unordered_map<PipelineStateDesc, uint32_t, PipelineStateDescHasher>
        m_lookup;              // desc → index
    uint32_t m_generation = 1; // Clear()마다 증가. 0은 빈 핸들이므로 건너뜀.
};
