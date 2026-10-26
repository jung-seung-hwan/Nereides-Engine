#pragma once
#include <cstdint>
#include <compare>     // C++20: operator<=> 기본 생성
#include <type_traits> // std::is_trivially_copyable_v

// GPU 리소스 핸들.
//
// 리소스는 포인터가 아니라 {index, generation} 값으로 넘김. 실제 객체는
// 백엔드(RHI/D3D11/, RHI/D3D12/)의 풀 안에 있고, 호출 코드는 D3D 타입을 전혀 모름.
// 목적은 7단계에서 RHI 인터페이스를 추출할 때 호출 코드가 바뀌지 않게 하는 것임
// (rendering-analysis D21의 (b) 방식).
//
// generation은 use-after-free를 잡음. 슬롯을 해제하면 세대가 1 올라가고,
// 이전 핸들은 index가 같아도 세대가 달라 풀에서 nullptr가 돌아옴.
// generation 0은 "빈 핸들"로 예약함. 풀의 세대는 1부터 시작함.
//
// Tag 타입은 BufferHandle과 TextureHandle을 서로 다른 타입으로 만들어
// 잘못 섞어 쓰면 컴파일 에러가 나게 하는 용도일 뿐이므로 정의는 필요 없음.
template <typename Tag>
struct Handle
{
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != UINT32_MAX && generation != 0;
    }

    // C++20 (2026-09-28): 비교 연산자를 = default 로 생성함.
    // <=> 를 default 로 선언하면 == 도 함께 암묵 선언되고, != / < / <= / > / >= 는 컴파일러가
    // 이 둘에서 재작성(rewritten comparison)함. 이전에는 == 와 != 를 손으로 썼음.
    // 멤버 순서(index → generation)의 사전식 비교이므로 std::map 키나 정렬에도 쓸 수 있음.
    auto operator<=>(const Handle&) const = default;
};

// C++20 (2026-09-28): ResourcePool<T, HandleT> 의 HandleT 를 제약하는 콘셉트.
// 이전에는 template <typename HandleT> 라 잘못된 타입을 넣으면 풀 내부 어딘가에서 알 수 없는 에러가 났음.
// 이제는 인스턴스화 지점에서 "ResourceHandle 을 만족하지 않음"이라고 바로 알려 줌.
//
// 2026-10-01: 조건을 "핸들처럼 생긴 타입"(index / generation / IsValid() 멤버 검사)에서
// "Handle<Tag> 의 특수화"로 바꿈. 멤버 검사는 덕 타이핑이라 모양만 같은 무관한 타입도 통과했음.
// kIsHandle 은 기본이 false 이고, T 가 Handle<Tag> 꼴일 때만 부분 특수화가 골라져 true 가 됨.
template <typename T>
inline constexpr bool kIsHandle = false; // 기본: 핸들이 아님
template <typename Tag>
inline constexpr bool kIsHandle<Handle<Tag>> =
    true; // 부분 특수화: T 가 Handle<Tag> 꼴이면 이쪽이 골라져 핸들로 인정


template <typename T>
concept ResourceHandle =
    kIsHandle<T> &&
    std::is_trivially_copyable_v<T>; // 핸들은 값으로 복사해 넘기는 작은 타입이어야 함

struct ShaderTag;
struct PipelineTag;
struct BufferTag;
struct TextureTag;
struct BindingLayoutTag;
struct ResourceSetTag;
struct SamplerTag;

using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;             // 5단계
using BindingLayoutHandle = Handle<BindingLayoutTag>; // 4단계: D3D12 루트 시그니처에 해당
using ResourceSetHandle = Handle<ResourceSetTag>;     // 4단계: 레이아웃에 맞는 핸들 묶음

static_assert(ResourceHandle<BufferHandle>, "Handle<Tag> 는 ResourceHandle 콘셉트를 만족해야 함");
static_assert(!ResourceHandle<uint32_t>,
              "Handle<Tag> 가 아닌 타입은 ResourceHandle 콘셉트를 만족하면 안 됨");
