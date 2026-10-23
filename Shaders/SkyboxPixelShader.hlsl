#include "Common.hlsli"

// 2026-10-08: 스카이박스 픽셀 셰이더. 정점 셰이더(SkyboxVertexShader)가 보간해 준 월드 방향으로 환경 큐브맵을 샘플함.
// 큐브맵이 sRGB 라벨이면 샘플할 때 하드웨어가 선형으로 풀고, 톤 패스의 sRGB 뷰가 다시 인코딩하므로 원본 색이 그대로 보임.
float4 PS_Sky(SkyVSOutput input) : SV_TARGET
{
    // 2026-10-08: HDR 큐브(1 을 넘는 값)도 옴. 2026/10/20 HDR: 선형 값 그대로 FP16 타깃에 씀 — 노출·톤매핑은 TonemapPixelShader 가 함.
    return float4(environmentMap.Sample(environmentSampler, input.direction).rgb, 1.0f);
}
