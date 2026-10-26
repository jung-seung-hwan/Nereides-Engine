# 12 Sherlock RHI 이식 (렌더러 포팅 1단계)

2026-10-19. Sherlock 렌더러 포팅의 첫 단계로, D3D11 RHI 계층을 `src/RHI/`에 같은 트리로 가져왔다. 이 단계에서는 RHI를 기존 `D3D11Renderer` 옆에서 함께 컴파일·링크하기만 하며, 어떤 코드도 아직 RHI를 호출하지 않는다. 렌더링 동작은 바뀌지 않았다. 전체 계획은 [렌더러 포팅 최종 계획](../renderer-porting-plan-final.html)을 따른다.

## 원본 기준

- 원본: `C:\Users\User\Desktop\SherlockEngine\SherlockEngine\RHI\` (Sherlock HEAD 9cbe7f3). 이 폴더는 Sherlock의 미커밋 변경 대상이 아니어서 작업 트리와 HEAD가 같다.
- 가져온 것: `RHI/` 루트 헤더와 `PipelineTypes.cpp`, `RHI.cpp`, `D3D11/` 전체, `DXBC/ShaderCompiler.cpp` (26개 파일).
- 가져오지 않은 것: `RHI/D3D12/` 전체.
- Sherlock의 파일명, 클래스명, 전역 `RHI` 네임스페이스는 그대로 두었다.

## 바꾼 것

| 구분 | 내용 |
|---|---|
| 인코딩·형식 | UTF-8 BOM 제거, 줄바꿈 LF, `.clang-format` 적용 (탭 → 공백 4칸). 기능 변경은 없다 |
| include | `pch.h` → `RHI/Compat/RHICommon.h`, `Core/Log.h` → `RHI/Compat/SherlockLog.h`, `Core/Paths.h` → `RHI/Compat/SherlockPaths.h` |
| `RHI.cpp` | D3D12 분기를 제거했다. `Backend::D3D12`를 요청하면 오류 로그를 남기고 `nullptr`을 반환한다 |
| 빌드 | `NereidesSandbox.vcxproj`/`.filters`를 `scripts/sync-project.ps1`로 갱신했다. 기존 항목 몇 개의 순서가 정렬되었다. `vcpkg.json`에 `directxtex`(dx11)를 추가했다 |

## 호환 계층 `src/RHI/Compat/`

Nereides에는 `Core/Log.h`가 이미 있고 API가 달라서 Sherlock 소스의 `Log::Error(...)` 호출을 그대로 둘 수 없었다. Nereides `Log`는 수정하지 않고 어댑터를 둔다.

- `RHICommon.h`: Sherlock `pch.h`가 제공하던 `windows.h`, `DirectXMath.h`, 표준 헤더.
- `SherlockLog.h/.cpp`: 전역 `Log::Debug/Info/Warn/Error`(printf 서식), `Log::HrToString`, `Log::ToUtf8`. 내용은 `nereides::Log::Write`로 전달한다. Nereides `Log`에는 Debug 레벨이 없어 Info로 보낸다.
- `SherlockPaths.h/.cpp`: `Paths::GetShaderPath/GetShaderBinaryPath/GetShaderSourcePath`. 지금은 모두 실행 파일 폴더의 `Shaders\` 아래를 가리킨다. Sherlock의 엔진 루트/프로젝트 루트 해석은 가져오지 않았다. 셰이더를 실제로 옮기는 4단계에서 Nereides의 경로 규칙과 `scripts/package.ps1` 복사 규칙에 맞춰 다시 정한다.

## 검증

| 항목 | 결과 |
|---|---|
| 기준선 (RHI 등록 전, Debug) | 8개 모드 모두 종료 코드 0 |
| Debug / Release 빌드 | 경고·오류 0 |
| Debug / Release 8개 모드 (`--engine-tests`, `--smoke-test`, `--scene-smoke-test`, `--editor-smoke-test`, `--editor-workflow-test`, `--presentation-smoke-test`, `--resize-smoke-test`, `--editor-resize-smoke-test`) | 16회 모두 종료 코드 0 |

주의: 이 작업 환경의 PowerShell에서는 `scripts/verify.ps1`이 `Start-Process`로 실행한 `--engine-tests`에서 종료 코드 2로 실패했다. 같은 실행 파일을 bash나 `cmd /c start /wait`로 실행하면 0이다. 코드 변경 전 기준선에서도 똑같이 실패했으므로 이번 변경과는 무관하다. 위 결과는 같은 모드를 직접 실행해서 얻었다. `Main.cpp`가 명령줄 전체를 `L"--engine-tests"`와 정확히 비교하므로, `Start-Process`가 공백이나 따옴표를 덧붙이면 실패하는 구조로 보이지만 확인하지는 않았다. `verify.ps1`과 `Main.cpp`는 엔진 쪽 파일이라 고치지 않았다. 팀 규칙대로 커밋 전에 사용자 터미널에서 `scripts\verify.ps1`을 한 번 돌려야 한다.

`--model-test`는 실행하지 않았다. RHI를 아직 호출하지 않아 모델 경로와 무관하다. 프로젝트는 `/utf-8`로 컴파일하므로(`NereidesSandbox.vcxproj`) BOM을 지운 RHI 소스의 한글 문자열도 그대로 해석된다.

이 단계는 RHI를 호출하지 않으므로 화면 출력은 달라지지 않는다. RHI 동작 검증(삼각형 출력)은 장치 소유권을 옮기는 2단계에서 한다.

## 남은 일

- 2단계: `RHI::Device`를 만들고 Present·UI 패스 계약을 적용한다.
- `Paths` 어댑터를 Nereides 경로 규칙으로 교체한다(4단계).
- `Log` 어댑터의 전역 `Log` 네임스페이스는 Sherlock 소스를 그대로 가져오기 위한 것이다. 렌더러(`Renderer.cpp`)를 옮길 때도 같은 어댑터를 쓴다.
