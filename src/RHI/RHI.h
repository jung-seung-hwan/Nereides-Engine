#pragma once
#include <memory>
#include "RHI/Device.h"
#include "RHI/CommandList.h"

// RHI 진입점. 백엔드를 고르는 유일한 자리.
//
// 호출 코드는 RHI::Device 와 RHI::CommandList 인터페이스, 핸들, Desc 만 봄.
// 백엔드 클래스(D3D11Device 등)의 이름은 RHI/RHI.cpp 와 RHI/<backend>/ 안에만 있음.
namespace RHI
{
// 실패하면 nullptr 를 돌려줌. 로그에 이유가 남음.
std::unique_ptr<Device> CreateDevice(Backend backend, const DeviceDesc& desc);

const char* ToString(Backend backend);
}
