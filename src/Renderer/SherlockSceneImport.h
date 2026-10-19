#pragma once
#include "Data/SceneIO.h"
#include <filesystem>

namespace nereides
{
// Sherlock 씬 JSON(Projects/*/Assets/Scenes/*.json)을 Nereides 씬으로 바꿈.
//
// 대응: 구·모델 객체 → MeshComponent(구 + 색·금속성·거칠기) / ModelComponent, 씬의 lights → LightComponent 객체,
// environment·ambientColor·clearColor → EnvironmentComponent 객체, CameraComponent 객체 → 카메라 역할과 시야각.
// 모델 경로는 assets/models/ 아래로 옮겨 읽고, 모델의 preTransform 은 Nereides 로더가 같은 회전을 이미 적용하므로 버림.
// 모델 재질은 모델 파일의 것을 씀. 알 수 없는 메시 종류는 건너뛰고 경고를 남김.
LoadedScene ImportSherlockScene(const std::filesystem::path& path, ModelCache& models);
} // namespace nereides
