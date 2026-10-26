#pragma once
#include "Renderer/Light.h"
#include "Scene/Scene.h"
#include <string>

namespace nereides
{
// 장면의 조명. 위치는 객체의 월드 위치, 빛이 진행하는 방향은 객체의 앞쪽(+Z)을 월드 회전으로 돌린 방향임.
// 씬에 켜진 조명이 하나라도 있으면 그 조명들이 렌더러의 기본 조명을 대신함 (첫 방향광이 그림자를 만듦).
class LightComponent final : public Component
{
public:
    LightType type = LightType::Directional;
    DirectX::XMFLOAT3 color{1, 1, 1};
    float intensity = 1;
    float range = 50;
    DirectX::XMFLOAT3 attenuation{1, 0.045f, 0.0075f}; // 상수, 일차, 이차 감쇠 (점·스포트)
    float innerConeCos = 0.9238795f;                    // 스포트 안쪽 원뿔 cos(22.5°)
    float outerConeCos = 0.8660254f;                    // 스포트 바깥 원뿔 cos(30°)
};

// 환경 큐브맵 묶음. 파일 이름은 씬에 직접 저장하지 않고 이름 있는 묶음으로 고름.
enum class EnvironmentPreset
{
    BuiltinSky,    // 렌더러가 만드는 절차적 하늘
    CubemapSample, // assets/textures/Cubemap/CubemapSample*.dds (HDR 스카이박스 + IBL 맵 3종)
    None           // 스카이박스·IBL 없음 (환경광 색만 사용)
};

// 장면의 하늘·간접광·출력. 켜진 첫 컴포넌트가 쓰이고, 없으면 렌더러 기본 환경을 씀.
class EnvironmentComponent final : public Component
{
public:
    EnvironmentPreset preset = EnvironmentPreset::BuiltinSky;
    float iblIntensity = 1;
    float exposure = 1;
    bool toneMapping = false;
    // 2026/10/23: 후처리. 기본값은 PostProcessSettings 와 같음 (전부 꺼짐). 순서는 렌더러가 고정함.
    bool blurEnabled = false;
    float blurRadius = 4;
    bool bloomEnabled = false;
    float bloomThreshold = 1;
    float bloomKnee = .5f;
    float bloomIntensity = .5f;
    float greyscale = 0;
    DirectX::XMFLOAT3 ambientColor{.12f, .12f, .12f}; // 환경 큐브가 없을 때의 환경광
    DirectX::XMFLOAT3 clearColor{0, 0, 0};            // 선형 색
};
} // namespace nereides
