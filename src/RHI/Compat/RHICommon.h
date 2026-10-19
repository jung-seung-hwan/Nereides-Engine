#pragma once

// Sherlock pch.h 대체. Nereides는 PCH를 쓰지 않으므로 RHI 소스가 기대하는 공통 헤더만 모은다.
// Direct3D 헤더는 여기에 두지 않는다(RHI/D3D11/D3D11Common.h 한 곳에서만 include).
#include <windows.h>

#include <DirectXMath.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
