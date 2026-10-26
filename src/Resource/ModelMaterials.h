#pragma once
#include "Resource/Model.h"

namespace nereides
{
// 모델별 설정을 먼저 찾고, 없으면 에셋 폴더의 공통 설정을 사용한다.
// 파일 경로는 설정 파일 기준이며, 설정에 없는 FBX 값은 보존한다.
void ApplyModelMaterials(const std::filesystem::path& modelPath,
                         std::vector<MaterialData>& materials);
}
