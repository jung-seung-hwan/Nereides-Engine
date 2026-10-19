#pragma once

// Direct3D 11 헤더를 모아 두는 유일한 자리.
//
// 규칙: d3d11.h / dxgi.h / d3dcompiler.h 를 직접 include하거나 ID3D11* 타입을
// 언급하는 파일은 RHI/D3D11/ 폴더 안에만 둠. 폴더 밖에서는 이 헤더도
// include하지 않음. 7단계에서 RHI 인터페이스를 분리할 때 "D3D11에 묶인 코드가
// 어디까지인가"에 폴더 경계 하나로 답하기 위함.
//
// (3단계 3.6까지는 pch.h가 같은 헤더를 전역으로 끌어오므로 규칙이 아직
//  강제되지 않음. 그 단계에서 pch.h의 D3D include를 지우고 grep으로 확인함.)

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_5.h> // IDXGIFactory2(CreateSwapChainForHwnd), IDXGIFactory5(ALLOW_TEARING 확인)
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib") // WKPDID_D3DDebugObjectName 등 GUID 정의

using Microsoft::WRL::ComPtr;
