#pragma once
#include <cstdint>
#include "RHI/Handle.h"
#include "RHI/ResourceDesc.h" // ShaderStageMask

// 바인딩 레이아웃과 리소스 셋. D3D 헤더 없음.
//
// BindingLayout = "이 파이프라인은 어떤 슬롯을 쓰는가"의 선언.
//   D3D12의 루트 시그니처에 해당함. D3D11에서는 슬롯 번호표일 뿐임.
// ResourceSet   = 그 선언의 각 슬롯에 실제로 꽂는 핸들 목록.
//   D3D12의 디스크립터 테이블에 해당함. D3D11에서는 SetResourceSet이
//   슬롯마다 *SSetConstantBuffers / *SSetShaderResources 호출로 풀림.
//
// 갱신 빈도별로 셋을 나눔 (프레임 / 오브젝트 / 재질). D3D12에서 디스크립터
// 테이블을 빈도별로 분리하는 관례와 같고, 드로우 정렬(PSO → Material → Mesh)의
// 단위이기도 함.

enum class BindingType : uint8_t
{
    ConstantBuffer, // b#  (CBV)
    ShaderResource, // t#  (SRV) — 텍스처
    Sampler,        // s#
};

struct BindingSlot
{
    BindingType type = BindingType::ConstantBuffer;
    uint8_t stageMask = 0; // ShaderStageMask_* 조합
    uint8_t reg = 0;       // 레지스터 번호 (b0의 0)
};

constexpr uint32_t kMaxBindingSlots = 16;

// 레지스터 상한. D3D11 한도(CBV 14, SRV 128, Sampler 16)보다 좁게 잡음.
// D3D12 루트 시그니처는 64 DWORD 예산이 있어 슬롯을 무한정 늘릴 수 없음.
constexpr uint8_t kMaxCbvRegister = 8;
constexpr uint8_t kMaxSrvRegister = 16;
constexpr uint8_t kMaxSamplerRegister = 8;

struct BindingLayoutDesc
{
    BindingSlot slots[kMaxBindingSlots];
    uint8_t slotCount = 0;
    const char* debugName = nullptr;

    BindingLayoutDesc& Add(BindingType type, uint8_t stageMask, uint8_t reg)
    {
        if (slotCount < kMaxBindingSlots)
        {
            slots[slotCount++] = BindingSlot{type, stageMask, reg};
        }
        return *this;
    }
};

// 슬롯 하나에 꽂는 리소스. 슬롯 타입에 따라 buffer / texture / sampler 중 하나만 사용함.
struct ResourceBinding
{
    BufferHandle buffer;
    TextureHandle texture;
    SamplerHandle sampler;
};

struct ResourceSetDesc
{
    BindingLayoutHandle layout;
    ResourceBinding bindings[kMaxBindingSlots]; // layout.slots[i] ↔ bindings[i]
    const char* debugName = nullptr;
};
