#include "Common.hlsli"

// 2026-10-08: 스카이박스. 정점 버퍼 없이 Draw(3) 으로 화면 전체를 덮는 삼각형 하나를 그리고, 픽셀마다 카메라에서 그 픽셀을
// 지나는 방향으로 환경 큐브맵(t9)을 샘플함 (SkyboxPixelShader). 큐브 메시를 그리는 방식과 결과는 같지만 정점·인덱스 버퍼가
// 필요 없고 컬링 방향을 신경 쓸 일도 없음.
//
// 깊이: 위치의 z 를 w 와 같게 두어 NDC 깊이를 1.0(가장 멂)으로 만듦. PSO 는 깊이 비교 LessEqual + 깊이 쓰기 끔이므로
// 불투명 물체가 이미 그려진 픽셀(깊이 < 1)은 통과하지 못하고, 아무것도 없는 픽셀(클리어 값 1.0)에만 하늘이 남음.
// 그래서 메인 패스의 불투명 드로우 "뒤에" 그림 — 가려질 픽셀은 깊이 테스트에서 떨어져 셰이딩하지 않음.
SkyVSOutput VS_Sky(uint vertexId : SV_VertexID)
{
    // vertexId 0, 1, 2 → uv (0,0) (2,0) (0,2) → NDC (−1, 1) (3, 1) (−1, −3). 화면 [−1, 1]² 를 덮는 직각삼각형 하나 (시계방향).
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    SkyVSOutput output;
    output.position = float4(ndc, 1.0f, 1.0f);   // z = w → 깊이 1.0

    // 투영의 역. 원근 투영(XMMatrixPerspectiveFovLH)은 x_ndc = x_view · P._11 / z_view, y_ndc = y_view · P._22 / z_view 이므로
    // z_view = 1 인 뷰 공간 점은 (x_ndc / P._11, y_ndc / P._22, 1). 셰이더의 proj 는 C++ 가 전치해서 올린 것을 column_major 로
    // 읽으므로 논리적으로 P 그 자체임 (ShaderConstants.h 의 행렬 관례).
    // 2026-10-15: 직교 투영(proj._44 == 1, 원근은 0)에서는 모든 픽셀의 시선 방향이 같음 — 위 식은 원근 전제라 방향이 퍼져 보였음.
    // 직교일 때는 카메라 앞 방향 하나로 고정함 (하늘은 단색에 가까워짐).
    const float3 viewDirection = proj._44 > 0.5f ? float3(0.0f, 0.0f, 1.0f) : float3(ndc.x / proj._11, ndc.y / proj._22, 1.0f);

    // 뷰 공간 → 월드 공간. view 의 3×3 부분 R 은 회전(직교 행렬)이라 역행렬이 전치임. 행벡터 규약에서 v·Rᵀ 는 mul(R, v) 와 같음.
    // 이동 성분(4행)은 쓰지 않음 — 하늘은 무한히 멀어서 카메라가 움직여도 따라오지 않아야 함.
    // 정점 셰이더에서 계산해도 되는 이유: 이 변환은 NDC 에 대해 선형이고, 세 정점의 w 가 모두 1 이라 보간도 화면 공간에서 선형임.
    output.direction = mul((float3x3)view, viewDirection);
    return output;
}
