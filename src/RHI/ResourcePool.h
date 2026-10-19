#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "RHI/Handle.h" // ResourceHandle 콘셉트

// 세대(generation) 카운터가 있는 슬롯 풀.
//
// 핸들 = {index, generation}. 슬롯을 해제하면 세대가 1 올라가므로, 해제 전에
// 발급된 핸들은 index가 같아도 Get()에서 nullptr가 돌아옴. 댕글링 포인터가
// "가끔 다른 객체를 가리키는" 대신 "항상 널"이 되는 것이 요점임.
// (Bitsquid의 ID lookup table, sokol_gfx의 pool과 같은 구조.)
//
// 빈 슬롯은 free list로 재사용함. generation 0은 빈 핸들로 예약되어 있으므로
// 세대는 1에서 시작하고, 값이 넘쳐 0이 되면 0을 건너뜀.
//
// C++20 (2026-09-28): HandleT 는 ResourceHandle 콘셉트(Handle.h)로 제약함. 요구 조건은
// Handle<Tag> 의 특수화이고 trivially copyable 일 것 (2026-10-01: 멤버 모양 검사에서 바꿈).
// 조건을 어기는 타입을 넣으면 풀 본문이 아니라 ResourcePool<...> 를 적은 줄에서 에러가 남.
template <typename T, ResourceHandle HandleT>
class ResourcePool
{
public:
    /*
	ResourcePool<D3D11Buffer, BufferHandle>이기 때문에 T는 D3D11Buffer이다

	D3D11Buffer 구조
	struct D3D11Buffer
	{
	BufferDesc desc;
	std::string name;                          // 힙 메모리
	ComPtr<ID3D11Buffer> buffers[kFrameCount]; // COM 참조 카운트
	};

	위 구조를 복사로 진행하면 Comptr에서 AddRef()가 호출되고 원본 소멸시 Release()도 호출 -> 불필요한 호출이고 잠시 소유자가 둘이 됨
	std::string또한 힙 할당이 일어나고 문자열 전체가 복사.
	이런 현상을 막기위해서 이동 연산자 사용, T&&이기 때문에 호출시 std::move써야함.
	*/
    HandleT Add(T&& item)
    {
        uint32_t index;
        if (!m_free.empty())
        {
            index = m_free.back();
            m_free.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }

        Slot& slot = m_slots[index];
        slot.item = std::move(item);
        slot.alive = true;
        return HandleT{index, slot.generation};
    }

    T* Get(HandleT handle)
    {
        if (handle.index >= m_slots.size())
            return nullptr;
        Slot& slot = m_slots[handle.index];
        if (!slot.alive || slot.generation != handle.generation)
            return nullptr;
        return &slot.item;
    }

    const T* Get(HandleT handle) const
    {
        return const_cast<ResourcePool*>(this)->Get(handle);
    }

    void Remove(HandleT handle)
    {
        if (Get(handle) == nullptr)
            return; // 이미 해제됐거나 세대가 다름
        Slot& slot = m_slots[handle.index];
        slot.item = T{}; // 스마트 포인터 멤버가 여기서 해제됨
        //빈 객체를 이동 대입하면서 기존 Comptr들이 Release()되면서 GPU리소스 해제
        slot.alive = false;
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;
        m_free.push_back(handle.index);
    }

    void Clear()
    {
        m_slots.clear();
        m_free.clear();
    }

    size_t AliveCount() const
    {
        return m_slots.size() - m_free.size();
    }

private:
    struct Slot
    {
        T item{};
        uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
};
