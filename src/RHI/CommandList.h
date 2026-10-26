#pragma once
#include <cstdint>
#include "RHI/Handle.h"
#include "RHI/PipelineTypes.h"
#include "RHI/RenderPassTypes.h"

// RHI::CommandList — GPU 명령을 기록하는 곳. 7단계에서 6단계 D3D11 CommandList 의 공개 API를
// 그대로 가상 인터페이스로 추출했음. 추출할 때 메서드 집합은 그대로였고, 10단계에서 WriteTimestamp 가 추가됐음.
//
// D3D11 백엔드: ImmediateContext 의 얇은 래퍼. "기록"이 곧 실행.
// D3D12 백엔드(8단계): 명령이 D3D12 그래픽스 커맨드 리스트에 쌓였다가 ExecuteCommandLists 로 제출됨.
//
// 이 헤더는 D3D 헤더를 include 하지 않음. 폴더 규칙: RHI/ 는 선언만, 구현은 RHI/D3D11/ 과 RHI/D3D12/.
namespace RHI
{
class CommandList
{
public:
    struct Stats
    {
        uint32_t renderPasses = 0;
        uint32_t barriers = 0;
        uint32_t srvUnbinds = 0; // 렌더 패스 시작 시 attachment 와 겹쳐서 바인딩을 풀어 준 SRV 수
    };

    virtual ~CommandList() = default;

    // ---- 렌더 패스 ----
    // 타깃 바인딩 + Load op(클리어) + 뷰포트. 패스 밖에서 Draw 를 부르면 에러 로그(1회)를 남기고 무시됨.
    virtual void BeginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void EndRenderPass() = 0;
    virtual bool IsInRenderPass() const = 0;

    // ---- 상태·바인딩 ----
    virtual void SetPipelineState(PipelineHandle handle) = 0;
    // 리소스 셋의 모든 슬롯을 바인딩함. D3D11: 슬롯마다 *SSet*. D3D12: SetGraphicsRootDescriptorTable.
    virtual void SetResourceSet(ResourceSetHandle handle) = 0;
    virtual void SetVertexBuffer(BufferHandle handle, uint32_t slot = 0,
                                 uint32_t offset = 0) = 0; // stride 는 desc.stride 를 씀
    virtual void SetIndexBuffer(BufferHandle handle, Format indexFormat = Format::R32_UINT) = 0;

    // ---- 드로우 ----
    virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                             int32_t baseVertex = 0) = 0;
    // 2026-10-08: 인덱스 없는 드로우. 정점 버퍼 없이 VS 가 SV_VertexID 로 위치를 만드는 패스(스카이박스의 전체 화면 삼각형)에 씀.
    // 그런 PSO 는 vertexLayout.attributeCount = 0 으로 만듦 (입력 레이아웃 없음).
    virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;

    // ---- 전이·복사 ----
    // D3D11: 상태 추적만 (before 가 기록과 다르면 Debug 경고). D3D12: ResourceBarrier(Transition).
    virtual void Barrier(TextureHandle texture, ResourceState before, ResourceState after) = 0;
    // 크기가 같은 두 버퍼 사이의 전체 복사.
    virtual void CopyBuffer(BufferHandle dst, BufferHandle src) = 0;

    // ---- 10단계: GPU 타임스탬프 (Profiler) ----
    // 이 시점의 GPU 시각을 slot 에 기록함 (0 ≤ slot < kMaxTimestamps, 프레임 안에서 증가 순서로).
    // 결과는 Device::GetTimestampResults 로 몇 프레임 뒤에 읽음. 렌더 패스 안팎 어디서든 부를 수 있음.
    virtual void WriteTimestamp(uint32_t slot) = 0;

    virtual const Stats& GetStats() const = 0;
    virtual void ResetStats() = 0;
};
}
