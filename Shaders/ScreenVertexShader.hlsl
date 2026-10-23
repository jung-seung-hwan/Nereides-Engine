// 2026-10-20: 후처리 패스 공용 VS. 정점 버퍼 없이 Draw(3) 으로 화면 전체를 덮는 삼각형 하나를 만듦.
// 색은 계산하지 않음 — 삼각형이 덮는 모든 픽셀에서 PS 가 한 번씩 돌게 하는 것이 전부이고, 실제 처리(톤매핑 등)는 각 패스의 PS 가 함.
// PS 는 SV_POSITION.xy 를 정수 좌표로 써서 입력 텍스처를 Load 하므로 UV 는 내보내지 않음.

float4 VS_Screen(uint vertexId : SV_VertexID) : SV_POSITION
{
    // vertexId 0, 1, 2 → uv (0,0) (2,0) (0,2) → NDC (−1, 1) (3, 1) (−1, −3). 화면 [−1, 1]² 를 덮는 직각삼각형 하나 (시계방향).
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}
