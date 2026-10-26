#include "RHI/Compat/RHICommon.h"
#include "RHI/RHI.h"
#include "RHI/D3D11/D3D11Device.h"
#include "RHI/Compat/SherlockLog.h"

// 백엔드 팩토리. 새 백엔드를 추가하려면 여기에 case 하나와 RHI/<backend>/ 폴더 하나를 더하면 됨.
// 이 파일은 백엔드 헤더를 include 하므로 RHI/ 의 "선언만" 규칙에서 유일한 예외임.

namespace RHI
{
const char* ToString(Backend backend)
{
    switch (backend)
    {
    case Backend::D3D11:
        return "D3D11";
    case Backend::D3D12:
        return "D3D12";
    default:
        return "?";
    }
}

std::unique_ptr<Device> CreateDevice(Backend backend, const DeviceDesc& desc)
{
    switch (backend)
    {
    case Backend::D3D11: {
        auto device = std::make_unique<D3D11Device>();
        if (!device->Init(desc))
        {
            Log::Error("RHI : D3D11 장치 초기화 실패.");
            return nullptr;
        }
        Log::Info("RHI : 백엔드 %s", ToString(backend));
        return device;
    }
    case Backend::D3D12:
        // Nereides는 D3D11 백엔드만 가져왔다(RHI/D3D12는 이식 범위 밖).
        Log::Error("RHI : D3D12 백엔드는 이 프로젝트에 포함되어 있지 않음.");
        return nullptr;
    default:
        Log::Error("RHI : 지원하지 않는 백엔드 %d", static_cast<int>(backend));
        return nullptr;
    }
}
}
