#pragma once

// 2026/10/23: 후처리 설정. 씬의 환경(SceneEnvironment::post)에 들어가 씬 JSON 으로 저장·복원·되돌리기됨.
// Renderer 는 이 값을 읽어 HDR 구간(블러·블룸)과 톤 패스(GreyScale 등 색 효과)를 정함.
// 효과를 추가할 때: 여기에 필드 → SceneSerializer 의 환경 JSON → DebugUI 의 후처리 섹션 → Renderer 의 후처리 체인 순으로 늘림.
//
// 순서는 Renderer 가 고정함 (사용자가 바꿀 수 없음):
//   HDR 구간: 블러 → 블룸   (선형 HDR 값에서 해야 1 을 넘는 밝기가 살아 있음)
//   톤 패스 : 노출 → ACES → 색 효과(GreyScale …)
struct PostProcessSettings
{
	// 가우시안 블러 (가로·세로 분리). 반경은 "720p 기준 픽셀"이라 출력 높이에 비례해 늘고 줆 — 씬 뷰 크기를 바꾸거나 카메라 미리보기(작은 타깃)에서도 모양이 같음.
	bool blurEnabled = false;
	float blurRadius = 4.0f;

	// 블룸: 밝기가 threshold 를 넘는 부분을 뽑아(knee 로 부드럽게) 축소 체인으로 흐린 뒤 intensity 만큼 더함.
	bool bloomEnabled = false;
	float bloomThreshold = 1.0f;
	float bloomKnee = 0.5f;
	float bloomIntensity = 0.5f;

	// 톤 패스의 색 효과. 0 = 원본, 1 = 완전한 흑백.
	float greyscale = 0.0f;

	bool AnyHdrEffect() const { return blurEnabled || bloomEnabled; }
};
