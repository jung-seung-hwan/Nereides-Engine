#include "Common.hlsli"

// 2026-10-20: 톤매핑 패스 픽셀 셰이더 (ScreenVertexShader 와 짝). HDR 씬 텍스처를 읽어 노출·ACES 를 적용함.
// 샘플러 없이 Load 를 쓰는 이유: 출력과 입력이 같은 크기면 1:1 대응이라 필터가 필요 없음.
// 2026/10/23: 출력이 입력보다 작을 수 있음 (단계별 썸네일). 좌표를 postView 의 입력/출력 비로 늘려 읽음 — 같은 크기면 비가 1 이라 이전과 같음.
// 결과는 선형 — 이 패스의 sRGB RTV 가 인코딩함.
Texture2D sceneHdr : register(t13);

float4 PS_Tonemap(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 scale = postView.xy / max(postView.zw, 1.0f);
    const float3 hdr = sceneHdr.Load(int3((int2)(position.xy * scale), 0)).rgb;

    // 디버그 뷰(알베도·노멀 등)는 조명 결과가 아니므로 노출·톤매핑을 거치지 않음 (기존에도 톤매핑 전에 반환했음).
    if (debugParams.x > 0.5f)
    {
        return float4(saturate(hdr), 1.0f);
    }
    float3 color = ApplyExposureToneMap(hdr);

    // 2026/10/23: 색 효과 (uber). 톤매핑을 끝낸 선형 값에 적용하고, 단계별 썸네일의 "효과 전" 그림은 postGrade.y 로 건너뜀.
    // 새 색 효과(대비·채도·비네트·그레인 …)는 여기에 이어 붙임 — 패스도 텍스처도 늘지 않음.
    if (postGrade.y < 0.5f)
    {
        // GreyScale: 휘도(Rec.709 계수)로 섞음. 0 = 원본, 1 = 완전한 흑백
        const float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = lerp(color, luma.xxx, saturate(postGrade.x));
    }
    return float4(color, 1.0f);
}
