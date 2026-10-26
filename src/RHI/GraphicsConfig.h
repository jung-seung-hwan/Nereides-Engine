#pragma once
#include <cstdint>

// 동시에 진행 중일 수 있는 프레임 수.
//
// D3D11에서는 드라이버가 CPU/GPU 동기화를 알아서 하므로 1이어도 동작함.
// 그래도 Dynamic 버퍼를 이 수만큼 복제해 두는 것은 D3D12 때문임.
// D3D12에서는 GPU가 아직 읽고 있는 버퍼를 CPU가 덮어쓰면 안 되므로,
// 프레임 N의 업로드는 슬롯 N % kFrameCount 에 하고 펜스로 프레임 N−kFrameCount 의 완료를 기다림.
// 8단계의 D3D12 백엔드가 이 구조로 구현되어 있음.
//
// kFrameCount 가 1..3 중 어느 값이든 화면은 같아야 함. 다르면 "이전 프레임 데이터에 기대는" 버그가 있다는 뜻임.
constexpr uint32_t kFrameCount = 2;

// 10단계: 프레임마다 기록할 수 있는 GPU 타임스탬프 슬롯 수 (Profiler). 슬롯은 0 부터 순서대로 씀.
constexpr uint32_t kMaxTimestamps = 32;
