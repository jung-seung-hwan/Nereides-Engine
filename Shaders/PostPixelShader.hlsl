#include "Common.hlsli"

// 2026/10/23: HDR 구간 후처리 픽셀 셰이더 (ScreenVertexShader 와 짝). 블러·블룸이 모드(postPass.x)로 갈림.
// 입력은 후처리 셋의 t13 (앞 단계의 결과), 파라미터는 b5 (PostParams). 선형 HDR 값을 그대로 다루므로 1 을 넘는 밝기가 살아 있음.
// 효과를 추가할 때는 모드를 하나 늘리거나 (같은 입력·출력 모양이면) 새 PS 파일을 만듦.
//
//   모드 1 : 가우시안 블러 한 방향 (postPass.yz = 방향, 가로 (1,0) / 세로 (0,1)). 입력·출력 크기가 같음.
//   모드 2 : 블룸 밝기 추출 + 축소 (hdr → 레벨 0). 임계값 아래는 버리고 knee 구간은 부드럽게 이어 줌.
//   모드 3 : 블룸 축소 (레벨 i-1 → 레벨 i). 선형 샘플러로 4x4 박스.
//   모드 4 : 블룸 확대 (레벨 i+1 → 레벨 i, 또는 레벨 0 → hdr). 3x3 텐트 필터에 postBloom.z 를 곱함.
//            출력 PSO 가 가산 블렌드(One/One)라 기존 값에 더해짐 — 입력과 출력이 같은 텍스처가 되지 않음.
Texture2D postInput : register(t13);
SamplerState postSampler : register(s6);

static const int kMaxBlurRadius = 64;

// 밝기(채널 최댓값)가 임계값을 넘는 만큼만 남김. knee 구간에서는 2차 곡선으로 부드럽게 올라감 (경계에서 튀지 않게).
float3 BrightPass(float3 c)
{
    const float threshold = postBloom.x;
    const float knee = postBloom.y;
    const float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1e-5f);
    const float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5f);
    return c * contribution;
}

float3 SampleInput(float2 uv)
{
    return postInput.SampleLevel(postSampler, uv, 0).rgb;
}

float4 PS_Post(float4 position : SV_POSITION) : SV_TARGET
{
    const int mode = (int)(postPass.x + 0.5f);

    if (mode == 1)
    {
        // Load + 클램프된 좌표. 가중치는 시그마에서 바로 계산함 (정규화는 합으로 나눔).
        const int2 base = (int2)position.xy;
        const int2 maxCoord = (int2)postView.xy - 1;
        const int2 dir = int2((int)postPass.y, (int)postPass.z);
        const int radius = min((int)postBlur.y, kMaxBlurRadius);
        const float sigma = max(postBlur.x, 0.01f);
        const float invTwoSigmaSq = 0.5f / (sigma * sigma);

        float3 sum = 0.0f;
        float weightSum = 0.0f;
        for (int i = -radius; i <= radius; ++i)
        {
            const float w = exp(-(float)(i * i) * invTwoSigmaSq);
            const int2 c = clamp(base + dir * i, int2(0, 0), maxCoord);
            sum += postInput.Load(int3(c, 0)).rgb * w;
            weightSum += w;
        }
        return float4(sum / weightSum, 1.0f);
    }

    // 모드 2~4 는 입력과 출력 크기가 다르므로 UV 로 읽음. position 은 픽셀 중심(x + 0.5)이라 출력 크기로 나누면 정확한 UV 임.
    const float2 uv = position.xy / postView.zw;
    const float2 texel = 1.0f / postView.xy;   // 입력 텍셀 크기

    if (mode == 2 || mode == 3)
    {
        // 출력 픽셀 중심 = 입력 텍셀 2x2 의 경계. 입력 텍셀 모서리에서 4 번 읽으면 (각각 2x2 평균) 4x4 박스가 됨.
        float3 a = SampleInput(uv + texel * float2(-1.0f, -1.0f));
        float3 b = SampleInput(uv + texel * float2( 1.0f, -1.0f));
        float3 c = SampleInput(uv + texel * float2(-1.0f,  1.0f));
        float3 d = SampleInput(uv + texel * float2( 1.0f,  1.0f));
        if (mode == 2)
        {
            a = BrightPass(a); b = BrightPass(b); c = BrightPass(c); d = BrightPass(d);
        }
        return float4((a + b + c + d) * 0.25f, 1.0f);
    }

    if (mode == 4)
    {
        // 3x3 텐트 (1 2 1 / 2 4 2 / 1 2 1) / 16. 입력(작은 쪽) 텍셀 단위로 읽음.
        float3 sum = 0.0f;
        sum += SampleInput(uv + texel * float2(-1.0f, -1.0f));
        sum += SampleInput(uv + texel * float2( 0.0f, -1.0f)) * 2.0f;
        sum += SampleInput(uv + texel * float2( 1.0f, -1.0f));
        sum += SampleInput(uv + texel * float2(-1.0f,  0.0f)) * 2.0f;
        sum += SampleInput(uv)                                 * 4.0f;
        sum += SampleInput(uv + texel * float2( 1.0f,  0.0f)) * 2.0f;
        sum += SampleInput(uv + texel * float2(-1.0f,  1.0f));
        sum += SampleInput(uv + texel * float2( 0.0f,  1.0f)) * 2.0f;
        sum += SampleInput(uv + texel * float2( 1.0f,  1.0f));
        return float4(sum * (postBloom.z / 16.0f), 0.0f);   // 알파 0: 가산 블렌드가 알파는 기존 값을 유지함
    }

    return float4(postInput.Load(int3((int2)position.xy, 0)).rgb, 1.0f);   // 알 수 없는 모드: 복사
}
