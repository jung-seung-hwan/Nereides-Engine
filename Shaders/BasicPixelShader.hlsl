#include "Common.hlsli"

float CalculateDistanceAttenuation(LightData light, float distanceToLight)
{
    const float denominator = max(
        light.attenuation.x +
        light.attenuation.y * distanceToLight +
        light.attenuation.z * distanceToLight * distanceToLight,
        EPSILON);

    const float safeRange = max(light.range, EPSILON);
    const float rangeFactor = saturate(1.0f - distanceToLight / safeRange);
    return (rangeFactor * rangeFactor) / denominator;
}

// 6단계: 그림자 맵 비교. 광원 클립 좌표 → 텍스처 UV → 3×3 PCF.
// 1 = 완전히 빛을 받음, 0 = 완전히 그림자. 맵 밖은 빛을 받는 것으로 간주함(Border 1).
float ComputeShadowFactor(float4 shadowPosition)
{
    // 단일 반환 구조: 조기 반환이 섞이면 fxc 가 -Od(디버그) 에서 X4000 오경고를 냄.
    float result = 1.0f;
    // 직교 투영이라 w = 1 이지만 관례대로 나눔.
    const float3 p = shadowPosition.xyz / shadowPosition.w;
    // NDC x,y ∈ [−1, 1] → UV. D3D 텍스처 공간은 v 가 아래쪽으로 증가하므로 y 부호를 뒤집음.
    const float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);
    // 그림자가 꺼져 있거나 광원 far 밖이면 result = 1 그대로.
    if (shadowParams.w >= 0.5f && p.z <= 1.0f)
    {
        const float depth = p.z - shadowParams.y;   // 수신자 깊이에서 바이어스를 뺌(자기 그림자 방지)

        // PCF: 이웃 텍셀 9개의 비교 결과(0/1)를 평균 냄. SampleCmp 는 비교 후 보간까지 하드웨어에서 처리함.
        float lit = 0.0f;
        [unroll]
        for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx)
            {
                lit += shadowMap.SampleCmpLevelZero(shadowSampler, uv, depth, int2(dx, dy));
            }
        }
        lit /= 9.0f;
        result = lerp(1.0f, lit, shadowParams.z);
    }
    return result;
}

// ---- 2026-10-08: PBR (Cook-Torrance, GGX) ----
// 직접광: f = kd · albedo / π + D · G · F / (4 · N·L · N·V).
// 조명 세기 관례: LightData 의 color × intensity 를 "빛에 수직인 면이 받는 조도 E" 로 봄. 그래서 BRDF 에 π 를 곱해
// 확산항이 albedo · E · N·L 이 됨 — Blinn-Phong 시절의 albedo · N·L · radiance 와 같은 밝기라 기존 씬의 조명 값을 그대로 씀.
float DistributionGGX(float NdotH, float alpha)
{
    // Trowbridge-Reitz. α = roughness² (Disney/Karis 의 지각적 거칠기 매핑)
    const float alpha2 = alpha * alpha;
    const float d = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(PI * d * d, EPSILON);
}

float GeometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotL, float NdotV, float roughness)
{
    // 직접광용 k = (roughness + 1)² / 8 (Karis 2013). 빛 방향과 시선 방향의 가림을 곱함.
    const float r = roughness + 1.0f;
    const float k = r * r / 8.0f;
    return GeometrySchlickGGX(NdotL, k) * GeometrySchlickGGX(NdotV, k);
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

// 환경광용. 거칠수록 스치는 각도의 반사가 덜 세지도록 최대값을 1 − roughness 로 누름 (Lagarde/Sébastien).
float3 FresnelSchlickRoughness(float3 F0, float cosTheta, float roughness)
{
    return F0 + (max((1.0f - roughness).xxx, F0) - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

// BRDF LUT 가 없을 때의 해석적 근사 (Karis, "Physically Based Shading on Mobile" 2014). 결과는 LUT 의 RG 와 같은 뜻.
float2 EnvBRDFApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// 환경광(IBL, split-sum). 확산 = (1 − F) · (1 − metallic) · albedo · 확산 조도(N),
// 반사 = 프리필터(R, 거칠기 밉) · (F0 · A + B), (A, B) = BRDF LUT(N·V, 1 − roughness).
// 환경 큐브가 없으면 두 조도를 ambientColor 하나로 대신함 — PBR 이전의 albedo · ambientColor 와 거의 같은 밝기.
float3 EnvironmentLighting(float3 albedo, float3 F0, float metallic, float roughness, float3 N, float3 V, float ao)
{
    const float NdotV = max(dot(N, V), 0.0f);
    const float3 F = FresnelSchlickRoughness(F0, NdotV, roughness);
    const float3 kd = (1.0f - F) * (1.0f - metallic);

    float3 irradiance = ambientColor;
    float3 prefiltered = ambientColor;
    float intensity = 1.0f;
    if (environmentParams.w > 0.5f)
    {
        irradiance = irradianceMap.SampleLevel(environmentSampler, N, environmentParams.z).rgb;
        const float maxLod = max(environmentParams.y - 1.0f, 0.0f);
        prefiltered = prefilteredMap.SampleLevel(environmentSampler, reflect(-V, N), roughness * maxLod).rgb;
        intensity = environmentParams.x;
    }
    const float2 brdf = outputParams.z > 0.5f
        ? brdfLut.SampleLevel(environmentSampler, float2(NdotV, 1.0f - roughness), 0.0f).rg
        : EnvBRDFApprox(roughness, NdotV);

    return (kd * albedo * irradiance + prefiltered * (F0 * brdf.x + brdf.y)) * intensity * ao;
}

float4 PS_Main(VSOutput input) : SV_TARGET
{
    // 재질의 기본색 × 정점 색 × 텍스처. sRGB 텍스처는 샘플링할 때 하드웨어가 선형으로 변환해 주므로
    // 여기서부터 모든 색은 선형 공간임. 백버퍼에 쓸 때 sRGB 뷰가 다시 인코딩함.
    const float4 texel = albedoTexture.Sample(albedoSampler, input.uv);
    const float4 surface = input.col * baseColor * texel;
    const float3 albedo = surface.rgb;

    // 9단계: 알파 컷아웃 (glTF MASK). 나뭇잎·격자처럼 알파로 구멍을 낸 재질에 씀.
    if (alphaCutoff > 0.0f)
    {
        clip(surface.a - alphaCutoff);
    }

    // 감마 검증용: 조명 없이 알베도를 그대로 출력함. 128 회색 텍스처가 화면에서 128로 읽혀야 함.
    // 2026/10/20 HDR: 톤매핑이 톤 패스로 옮겨 가서 이 픽셀도 노출·ACES 를 거침 — 감마 검증은 톤매핑을 끄고 노출 1 에서 할 것.
    if (unlit > 0.5f)
    {
        return float4(albedo, surface.a);
    }

    // 2026-10-08: PBR 입력. 텍스처가 없으면 흰색이라 계수가 그대로 쓰임. 거칠기 하한은 GGX 의 0 나눗셈과 점광원의 바늘 같은 하이라이트를 막음.
    const float4 metalRough = metallicRoughnessTexture.Sample(albedoSampler, input.uv);
    const float metal = saturate(metallic * metalRough.b);
    const float rough = clamp(roughness * metalRough.g, 0.03f, 1.0f);
    const float ao = lerp(1.0f, occlusionTexture.Sample(albedoSampler, input.uv).r, occlusionStrength);
    const float3 emission = emissive * emissiveTexture.Sample(albedoSampler, input.uv).rgb;
    // F0: 수직으로 볼 때의 반사율. 유전체는 4%, 금속은 자기 색. 금속은 확산이 없음(kd 의 1 − metallic).
    const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metal);

    // 노멀 매핑. 탄젠트 공간(T, B, N) 의 노멀 맵 값을 월드 공간으로 변환함.
    // 노멀 맵은 선형 텍스처이고 (0.5, 0.5, 1) 이 "평평"을 뜻함. B 는 정점 탄젠트의 손잡이(w)로 복원함.
    const float3 geometricNormal = SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float3 T = SafeNormalize(input.tangent.xyz - geometricNormal * dot(geometricNormal, input.tangent.xyz), float3(1.0f, 0.0f, 0.0f));
    const float3 B = cross(geometricNormal, T) * input.tangent.w;
    float3 tangentNormal = normalTexture.Sample(albedoSampler, input.uv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalStrength;
    const float3 normal = SafeNormalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * geometricNormal, geometricNormal);

    const float3 viewDirection = SafeNormalize(
        cameraPosition - input.worldPosition, float3(0.0f, 0.0f, -1.0f));

    const float shadowFactor = ComputeShadowFactor(input.shadowPosition);

    // 11단계: 디버그 뷰. 에디터의 Render Settings 에서 고름.
    const int debugView = (int)debugParams.x;
    if (debugView == 1) return float4(albedo, 1.0f);
    if (debugView == 2) return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugView == 3) { const float d = saturate(length(cameraPosition - input.worldPosition) / max(debugParams.y, 0.001f)); return float4(d, d, d, 1.0f); }
    if (debugView == 4) return float4(shadowFactor, shadowFactor, shadowFactor, 1.0f);
    if (debugView == 5) return float4(frac(input.uv), 0.0f, 1.0f);
    if (debugView == 6) return float4(0.0f, rough, metal, 1.0f);   // glTF 의 metallicRoughness 텍스처와 같은 채널 배치
    if (debugView == 7) return float4(ao, ao, ao, 1.0f);

    const float NdotV = max(dot(normal, viewDirection), EPSILON);
    float3 finalColor = EnvironmentLighting(albedo, F0, metal, rough, normal, viewDirection, ao) + emission;
    const uint activeLightCount = min(lightCount, MAX_LIGHTS);

    [loop]
    for (uint i = 0; i < activeLightCount; ++i)
    {
        const LightData light = lights[i];
        float3 surfaceToLight = float3(0.0f, 1.0f, 0.0f);
        float attenuation = 1.0f;

        if (light.type == LIGHT_DIRECTIONAL)
        {
            surfaceToLight = SafeNormalize(-light.direction, float3(0.0f, 1.0f, 0.0f));
            // 그림자 맵은 방향광 0 하나에 대해서만 만듦.
            if (i == 0)
            {
                attenuation *= shadowFactor;
            }
        }
        else
        {
            const float3 toLight = light.position - input.worldPosition;
            const float distanceToLight = length(toLight);
            surfaceToLight = SafeNormalize(toLight, float3(0.0f, 1.0f, 0.0f));
            attenuation = CalculateDistanceAttenuation(light, distanceToLight);

            if (light.type == LIGHT_SPOT)
            {
                const float3 lightDirection = SafeNormalize(
                    light.direction, float3(0.0f, -1.0f, 0.0f));
                const float3 lightToSurface = -surfaceToLight;
                const float coneCos = dot(lightDirection, lightToSurface);
                const float coneWidth = max(light.innerConeCos - light.outerConeCos, EPSILON);
                attenuation *= saturate((coneCos - light.outerConeCos) / coneWidth);
            }
        }

        const float NdotL = saturate(dot(normal, surfaceToLight));
        if (NdotL <= 0.0f)
        {
            continue;
        }
        const float3 halfVector = SafeNormalize(surfaceToLight + viewDirection, normal);
        const float NdotH = saturate(dot(normal, halfVector));
        const float VdotH = saturate(dot(viewDirection, halfVector));

        const float D = DistributionGGX(NdotH, rough * rough);
        const float G = GeometrySmith(NdotL, NdotV, rough);
        const float3 F = FresnelSchlick(F0, VdotH);
        const float3 specular = D * G * F / max(4.0f * NdotL * NdotV, EPSILON);
        const float3 kd = (1.0f - F) * (1.0f - metal);   // 반사된 몫과 금속을 뺀 나머지만 확산함 (에너지 보존)

        // 조도 관례(위 주석): BRDF 전체에 π 를 곱하므로 확산항은 kd · albedo, 반사항은 specular · π 가 됨.
        const float3 irradiance = light.color * max(light.intensity, 0.0f) * attenuation * NdotL;
        finalColor += (kd * albedo + specular * PI) * irradiance;
    }

    // 2026/10/20 HDR: 선형 HDR 값을 그대로 FP16 타깃에 씀 (1 을 넘어도 잘리지 않음). 노출·ACES 는 TonemapPixelShader 가 함.
    return float4(finalColor, surface.a);
}
