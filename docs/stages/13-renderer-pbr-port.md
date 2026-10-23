# 13 Sherlock 렌더러 이식 (렌더러 포팅 2~6단계)

2026-10-19. 1단계(RHI, [12](12-renderer-rhi-port.md)) 위에 Sherlock `Renderer`를 올려, 기존 단색 조명 렌더러를 PBR(Cook-Torrance)·IBL·스카이박스·그림자·스키닝 렌더러로 바꿨다. 입력 경계는 `RenderFrame`이며, 에디터·테스트가 쓰던 `D3D11Renderer` 호출 방식은 유지했다. 전체 계획은 [렌더러 포팅 최종 계획](../renderer-porting-plan-final.html)을 따른다.

## 구조

- `D3D11Renderer`(nereides)는 얇은 앞면이다. `RHI::Device`(스왑체인 소유)와 Sherlock `Renderer`를 갖고 `Initialize/Resize/PrepareSceneView/Render/RenderScene/SetOverlay/RequestCapture`를 그대로 제공한다. 새로 `SetVSync(bool)`, `Settings()`가 있다.
- 프레임 순서: BeginFrame → `Renderer::Render`(그림자 패스 2048² → 메인 패스) → 선택적 `VerifySceneView` → UI 패스(overlay, ImGui) → 읽기 요청 → EndFrame(Present).
- `Renderer`, `TextureLoader`, `Light.h`, `Material.h`, `ShaderConstants.h`, `VertexTypes.h`는 Sherlock 구조·전역 네임스페이스를 유지했다.
- 장치는 렌더러가 소유하고 ImGui는 `Device()/Context()` 접근자로 초기화한다.

## 단계별 변경

| 단계 | 내용 |
|---|---|
| 2 장치·Present | 스왑체인을 `D3D11Device`가 소유. 장면 렌더는 Present 없이 끝나고 UI 패스 뒤 EndFrame에서 Present. 최소화·0 크기 동작은 기존과 같다 |
| 3 `RenderFrame` 확장 | `DrawItem`이 전체 `Material`을 가짐(`tint` 대체). `RenderEnvironment`, 조명 목록(최대 8, 그림자 광원은 첫 방향광), 지우기 색 추가. `MeshVertex`에 tangent. `MakeCube`는 24정점(법선·UV·탄젠트) |
| 4 정적 PBR | 셰이더 7종을 `Shaders/`에 두고 msbuild `FxCompile`로 `.cso` 생성. 텍스처는 경로 기준으로 DirectXTex가 로드. 메시/머티리얼 GPU 캐시는 포인터 + `weak_ptr` 소유자 검사로 키를 잡고 매 프레임 정리 |
| 5 스키닝 | `VERTEX_SKINNED`(96B), 본 cbuffer b4(128개). 가중치가 있으면 스키닝 포맷으로 올리고, 가중치 합이 0인 정점은 스키닝을 건너뜀. 그림자 패스도 스키닝 지원 |
| 6 그림자·성능 | 2048² D32 그림자 맵, `shadowCenter` 설정, 디버그 뷰. 아래 성능 참고 |

## Sherlock 원본과 다른 점

- 카메라 위치는 별도 필드 없이 `frame.view` 역행렬에서 구한다. 에디터·테스트의 뷰 덮어쓰기와 항상 일치한다.
- 미리보기 카메라, `InvalidateScene`, Profiler는 가져오지 않았다.
- 모델에 내장된 텍스처(`*N`)는 지원하지 않고 흰색으로 대체한다.
- 텍스처 경로 규칙: `builtin:*`, 절대 경로, 또는 현재 폴더 기준 상대 경로 → `assets/textures`. (`Material.h` 안의 Sherlock Assets 규칙 주석은 아직 옛 설명이다.)
- 셰이더 경로 어댑터(`SherlockPaths`)는 Nereides 전용이다. 릴리스는 실행 파일 옆 `Shaders\`, Debug는 소스 트리 `Shaders\`를 보고 원본이 더 새로우면 다시 컴파일하며, 파일 변경은 500 ms 주기 시계 폴링으로 감지한다.
- 조명이 8개를 넘으면 경고를 한 번 남기고 나머지는 무시한다.
- 기존 `MeshShaders.h`(단색 셰이더)는 삭제했다.
- Debug에서는 옛 렌더러처럼 D3D11 CORRUPTION/ERROR에서 중단한다. COM은 `Initialize`에서 초기화한다(DirectXTex WIC 경로가 필요로 함).
- 기본 삼각형(`--smoke-test`)은 RHI를 거쳐 그리고 읽기 결과로 확인한다.
- 클리어 색은 선형 값 `{0.001935, 0.010023, 0.027140}`이며, sRGB로 보면 이전과 같은 (6,26,46)이다.

## 검증 기준 변경

장면 검증은 조명 결과가 바뀌므로 색 일치 대신 다음으로 바꿨다.

- `LooksRendered`: 밝은 픽셀(합 > 150) 16개 초과, 모서리 픽셀과 30 넘게 다른 픽셀 16개 초과.
- 해시·캡처 경로: 클리어 색(6,26,46)이 아닌 픽셀 16개 초과.
- 기존 삼각형 검사는 그대로다.

`--engine-tests`에 `TestRenderPipeline`을 추가했다: 큐브 정점·법선·탄젠트·와인딩, 머티리얼 재사용·재생성, `RenderFrame` 기본값, 4×4 BMP sRGB 3-mip 로드와 누락 파일 보고, GPU 해시 반복성, 머티리얼 교체·메시 교체(오래된 캐시 없음), 항등 스킨 = 정적, 본 이동 시 변화, 조명 12개 허용.

## 검증 결과 (Debug·Release 모두)

- 빌드 경고·오류 0.
- 8개 모드 종료 코드 0: `--engine-tests`, `--smoke-test`, `--scene-smoke-test`, `--editor-smoke-test`, `--editor-workflow-test`, `--presentation-smoke-test`, `--resize-smoke-test`, `--editor-resize-smoke-test`.
- `--model-test`(스키닝된 glTF, 임시 생성): "GPU animation pixel change=yes". 캡처에서 굽힘과 그림자 모양이 일치함을 눈으로 확인했다.
- 이 환경에서는 `scripts/verify.ps1`이 `Start-Process` 때문에 `Main.cpp` 종료 코드 2로 실패한다(포팅 전 기준선도 동일). 그래서 모드를 `timeout 40 ./bin/x64/<구성>/NereidesSandbox.exe <모드>`로 직접 실행했다.

## 성능 (Release, 1280×720, `--benchmark`, 샘플 180)

| 수직 동기화 | 평균 | p95 | 작업 집합 |
|---|---|---|---|
| 켬 (기본) | 16.68 ms | 16.82 ms | 52.5 MiB |
| 끔 (`NEREIDES_VSYNC=0`) | 0.114 ms | 0.119 ms | 52.1 MiB |

- 이 값은 Present를 포함한 CPU 벽시계 시간이며 GPU 시간이 아니다. VSync를 끄면 Present가 비동기라 CPU 제출 비용에 가깝다. 장면이 큐브 몇 개뿐이라 FHD 측정은 하지 않았다. 실제 모델 장면에서 다시 재야 한다.
- `NEREIDES_VSYNC=0`은 `--benchmark`에서만 읽는다.

## 남은 일

- 7단계(HUD/트레일/텔레그래프): 명세가 없어 착수하지 않았다. 담당과 요구사항 확인이 필요하다.
- 8단계 정리: 에디터 렌더 설정 UI, `Material.h` 주석 갱신.
- `scripts/package.ps1`이 `Shaders\`를 함께 복사하도록 고쳤다.
- 협의 대상: `NereidesSandbox.vcxproj`, `vcpkg.json`, `Dependencies.props`(DirectXTex, 셰이더 빌드 항목), `RenderFrame`/`Model` 계약 변경.

## RenderTestScene (Sherlock CubemapTest 장면 재연)

Sherlock `Projects/CubemapTest/Assets/Scenes/Main.json`을 `data/render-test/CubemapTest.json`으로 복사해 그대로 읽는 시험 장면이다. Scene/에디터를 거치지 않고 JSON → `RenderFrame` → 렌더러만 검사한다.

- 실행: `./bin/x64/<구성>/NereidesSandbox.exe --model-test data/render-test/CubemapTest.json` → `logs/render-test-scene.bmp` 저장. `Main.cpp`를 건드리지 않으려고 `RunModelTest`가 `.json` 확장자를 `RunRenderTestScene`(`src/Renderer/RenderTestScene.cpp`)으로 넘긴다.
- 재연 범위: 카메라(CameraComponent), 방향광, 환경(HDR 큐브 4종, iblIntensity 0.5, toneMapping), 재질 14개, DamagedHelmet, 구 12개(금속 R0.0~1.0 / 플라스틱 R0.0~1.0 각 6개).
- 필요한 에셋(Git 제외, `assets/**`): `assets/textures/Cubemap/CubemapSample{EnvHDR,DiffuseHDR,SpecularHDR,Brdf}.dds`, `assets/models/DamagedHelmet/DamagedHelmet.glb`. Sherlock `Assets/`에서 복사했다(약 70 MB).
- 이 작업으로 추가된 것: 모델 내장 텍스처 지원(`EmbeddedTextures.h`, 이름 `<모델파일명>/<번호>`; 이전 단계의 "내장 텍스처 미지원"은 해소), glTF 오클루전 텍스처의 `LIGHTMAP` 슬롯 대체 조회, 모델 탄젠트 손잡이 부호 반전(Sherlock `ComputeTangents(bitangentTowardsIncreasingV=false)`와 같은 규칙).
- Sherlock와 다른 점: 모델의 `preTransform`은 곱하지 않는다. Nereides 로더가 glb 루트 노드의 +90° X 회전을 이미 적용하며 결과 행렬이 `preTransform`과 같다(곱하면 헬멧이 뒤집힌다).
- 확인: Debug·Release 모두 종료 코드 0, 같은 프레임을 두 번 그려 해시 동일. Sherlock 화면과 나란히 비교는 아직 하지 않았다.

### Sherlock 캡처와의 비교 (같은 장면, 1280×720)

`SherlockGame.exe --project=...CubemapTest.sherlock --scene=Main.json --exit-after=120 --screenshot=<png>`로 Sherlock 쪽을 캡처해 `RenderTestScene` 결과와 픽셀 차이를 냈다.

- 하늘과 구 12개: 차이 평균 0.0/255 (완전히 같음).
- 헬멧: 처음에는 전체가 푸르스름하게 떠 보였다. 원인은 텍스처 번호가 다른 것이었다. Sherlock 씬의 `DamagedHelmet/N`은 glTF 이미지 번호(emissive=2, normal=4)이고 Assimp 내장 텍스처 번호는 사용 순서(normal=2, emissive=4)여서 노멀·발광이 서로 바뀌어 읽혔다. 씬이 모델 안 이미지를 가리키면 모델 파일에서 읽은 같은 칸의 텍스처로 대체하도록 고친 뒤 헬멧 영역 차이 평균이 약 1.2/255로 줄었다.
- 남은 차이: 노멀맵 가장자리의 미세한 음영. Sherlock은 정점 탄젠트를 Lengyel 방식으로 직접 계산하고 Nereides 로더는 Assimp 탄젠트를 쓰는 차이로 보인다. Sherlock 캡처는 4× MSAA이고 Nereides는 MSAA가 없다.
- 렌더 관련 파일은 `src/Renderer/`로 모았다 (Renderer, TextureLoader, Light, Material, ShaderConstants, VertexTypes, EmbeddedTextures, RenderTestScene). 프로젝트 필터는 `Source Files\Renderer`, `Header Files\Renderer`이며 `scripts/sync-project.ps1`이 이를 유지한다. `src/RHI/`는 별도 라이브러리라 그대로 두었다.

### Nereides 씬으로 변환 (에디터에서 열기)

`data/scenes/CubemapTest.json`이 Sherlock `CubemapTest` 장면을 Nereides 씬 형식(version 1)으로 바꾼 것이다. 에디터의 File > Open 으로 연다.

- 변환기: `src/Renderer/SherlockSceneImport.cpp`. `--model-test data/render-test/CubemapTest.json`을 실행하면 변환 → `data/scenes/`에 저장 → 저장한 파일을 다시 읽어 렌더 → `logs/render-test-scene.bmp` 순으로 돈다(같은 내용으로 다시 써지므로 몇 번 실행해도 같다).
- 씬에 새로 생긴 것(모두 기존 씬 파일과 호환, 기본값은 이전 동작 그대로):
  - `MeshComponent`: `metallic`, `roughness`(에디터 인스펙터에 슬라이더), 구 메시(`"type": "sphere"` + radius/slices/stacks). `MakeSphere`는 Sherlock `CreateSphere`와 같은 배치.
  - `LightComponent`(`"light"`): 위치 = 객체 위치, 빛 방향 = 객체의 +Z를 월드 회전으로 돌린 방향. 켜진 조명이 하나라도 있으면 렌더러 기본 조명을 대신한다.
  - `EnvironmentComponent`(`"environment"`): 하늘/IBL 묶음(`builtinSky`, `cubemapSample`, `none`), iblIntensity, exposure, toneMapping, ambient, clear color. 컴포넌트 속성은 열거형만 문자열을 받으므로 큐브맵 파일 이름을 직접 적지 않고 묶음으로 고른다. `cubemapSample`은 `assets/textures/Cubemap/CubemapSample*.dds` 4개.
- 씬 구성: Main Camera(0, 3.2, -9, fov 45°), Directional Light, Environment, DamagedHelmet(`model`, 재질은 glb의 것), Gold/Plastic 구 12개.
- 검증: `--engine-tests`의 `TestRenderSceneComponents`(Encode→Decode→Encode 동일, 재질·조명·환경이 `RenderFrame`에 반영, 조명 없는 씬은 기본값 유지)와 렌더 시험의 조명 방향 확인. Sherlock 캡처와 전체 화면 평균 차이 0.17/255.
- 주의: 이 씬은 `assets/` 의 큐브맵·헬멧 파일이 있어야 제대로 보인다(`assets/**`는 git 제외, 공유 드라이브에서 받음).

## HDR 렌더 타깃·톤 패스·후처리 반영 (2026-10-23)

Sherlock에서 2b0f6a4 이후 추가된 e716094(HDR·톤매핑 패스)와 5fa4669(후처리 PP)를 옮겼다. 포팅 때 기준은 `2b0f6a4`였고, 그 뒤 Sherlock 변경은 RHI를 건드리지 않아 `src/RHI`는 그대로다.

- 메인 패스는 선형 `R16G16B16A16_FLOAT` 텍스처(`m_hdrColor`)에 그린다. 메인·스카이 PSO의 RTV 포맷도 FP16이다. 노출·ACES는 `BasicPixelShader`/`SkyboxPixelShader`가 아니라 `TonemapPixelShader`가 하고, sRGB 인코딩은 톤 패스의 sRGB RTV가 한다.
- 프레임 순서: Shadow → Main(HDR) → 블러·블룸(`RendererPost.cpp`, HDR 텍스처 제자리) → Tonemap(씬 뷰 텍스처 또는 백버퍼) → UI.
- 크기: 에디터(오프스크린)는 씬 뷰 크기, 게임 모드는 `Renderer::SetOutputSize`로 받은 백버퍼 크기. `D3D11Renderer`가 `Initialize`/`Resize`에서 알려 준다.
- 새 셰이더: `ScreenVertexShader`(풀스크린 삼각형), `TonemapPixelShader`, `PostPixelShader`(가우시안 블러·블룸). `NereidesSandbox.vcxproj`에 `FxCompile` 항목을 추가했다. 톤 패스의 색 효과(GreyScale)도 포함.
- 설정: `PostProcessSettings`(`src/Renderer/PostProcessSettings.h`)가 `RenderEnvironment::post`로 들어온다. 씬에서는 `EnvironmentComponent`의 필드(`blurEnabled`, `blurRadius`, `bloomEnabled`, `bloomThreshold`, `bloomKnee`, `bloomIntensity`, `greyscale`)이며 인스펙터와 씬 파일에 자동으로 연결된다. 옛 씬 파일은 키가 없으므로 전부 꺼진 기본값이다.
- Sherlock와 다른 점:
  - PostParams 상수버퍼는 **b5**다. Sherlock은 b4인데 Nereides는 b4를 뼈 팔레트가 쓴다(`Common.hlsli`에 두 cbuffer가 같은 레지스터를 쓰면 안 되므로).
  - 카메라 미리보기용 HDR·스크래치는 가져오지 않았다(미리보기 자체가 없음). `GpuProfileScope`(Profiler)도 없어 뺐다.
  - 단계별 썸네일(`RequestPostCapture`/`GetPostStage`)은 렌더러 쪽 API까지만 옮겼고, Sherlock `DebugUI`의 "Post Stages" 창은 아직 에디터에 붙이지 않았다.
- 검증: Debug·Release 빌드 경고·오류 0, 8개 모드와 `--model-test data/render-test/CubemapTest.json` 종료 코드 0. `--engine-tests`에 후처리 검사를 추가했다(블러·블룸·GreyScale이 화면을 바꾸고, 꺼 두면 이전과 같은 해시, 환경 컴포넌트 값이 `RenderFrame`에 전달됨).
- 주의: 감마 검증용 `unlit` 재질도 이제 톤 패스를 거친다. 감마를 확인하려면 톤매핑을 끄고 노출 1에서 볼 것.

