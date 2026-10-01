# 기술검토서 — 0단계: 환경 및 첫 구현 범위

조사일: 2026-10-01 (Asia/Seoul)

## 요구사항과 현재 상태

검과 총을 사용하는 3인칭 크라켄 보스전. 폭풍 속 배 3척을 이동하는 3페이즈 구조이며, 팀은 엔진 1명 / 렌더링 1명 / 콘텐츠 2명이다. 최종 발표는 2026-12-18이다. 이번 단계에서는 전체 엔진 구조나 게임 시스템을 확정하지 않는다.

사용자 확인에 따라 `C:\Users\User\Desktop\Nereides Engine`을 사용한다. 처음 지정한 `Nereid Engine`은 존재하지 않았다. 현재 폴더에는 기획, 이미지 레퍼런스, MMD 자료 폴더가 있으며 기존 엔진 프로젝트와 Git 저장소는 없었다. 자료는 수정하지 않았다.

## 로컬 도구 확인

| 항목 | 확인 결과 |
| --- | --- |
| Git | 2.54.0.windows.1, 일반 터미널에서 실행 가능 |
| Visual Studio | Community 2022 17.14 / Community 2026 18.7 설치 |
| C++ 도구 | vswhere에서 두 설치의 x86/x64 C++ 도구 구성 확인, VS2022 MSVC 버전 폴더 14.44.35207 확인 |
| Windows SDK | 10.0.22621.0 / 10.0.26100.0 Include 폴더 확인 |
| CMake | VS2026 번들 4.3.1-msvc1 실행 확인 |
| MSBuild | VS2026 번들 18.7.8.30822 실행 확인 |
| DX11 디버그 레이어 | System32의 D3D11SDKLayers.dll 존재 확인 |
| vcpkg | SherlockEngine의 manifest와 설치 폴더 존재. VCPKG_ROOT 환경변수는 확인되지 않음 |

일반 PowerShell PATH에서는 cl / msbuild / cmake가 검색되지 않았다. 설치 누락으로 단정할 수 없으며 VS 개발자 터미널 또는 확인한 절대 경로를 사용한다. 새 프로젝트의 컴파일·링크·GPU 실행과 디버그 레이어 활성화는 아직 검증하지 않았다.

## SherlockEngine 관련 구조

기준 경로: `C:\Users\User\Documents\GitHub\SherlockEngine`. 조회 시 main...origin/main이며 작업 트리 변경은 없다. 참고 저장소는 수정하지 않았다. Git 소유자 차이 때문에 상태 조회에 명령 한정 safe.directory 옵션을 사용했고 전역 설정은 변경하지 않았다.

| 파일 | 역할 / 참고할 이유 | 통째로 가져올 때의 의존성 |
| --- | --- | --- |
| SherlockGame/GameMain.cpp | 실행 진입점 | GameApp, 엔진 실행 구성 |
| SherlockEngine/App/AppBase.cpp, .h | Win32 창, 메시지 루프, 초기화·종료 순서, 크기 변경 | Engine, Input, Log, 설정, 시간·프로파일러, ImGui, ImGuizmo |
| SherlockEngine/RHI/D3D11/D3D11Device.cpp, .h | 장치·스왑체인, 백버퍼, 리사이즈, COM 수명, 디버그 보고 | 공통 RHI, 리소스 핸들·풀, 변환 코드, 커맨드 리스트, Log, pch, ImGui DX11 |
| SherlockEngine/RHI/D3D11/D3D11CommandList.cpp | 렌더 타깃 바인딩과 ClearRenderTargetView | RHI 렌더 패스와 리소스 시스템 |
| SherlockEngine/Core/Input.h, .cpp | 입력 질의 API, 포커스 상실 처리 | Win32 메시지 공급과 프레임 경계 규칙. DirectXTK 입력 래퍼 구현은 아님 |
| SherlockEngine/Core/Log.h | 오류·HRESULT, VS 출력 창·파일 로그 설계 참고 | 로그 구현과 싱크 구성 |
| vcpkg.json | 의존성 관리 방식 | directxtk 외에 DX12, 에디터, 모델·텍스처용 패키지도 포함 |

참고 엔진은 DX11/DX12 공통 RHI와 에디터를 포함한다. 첫 화면을 위해 이 계층을 모두 가져오면 작은 변경도 많은 시스템에 의존하게 된다. 창과 DX11 초기화 흐름, 자원 해제 순서만 참고하여 새 코드를 작성하는 것을 제안한다.

실제로 복사한 SherlockEngine 코드는 현재 0개다. 향후 복사할 경우 원본 경로·커밋·범위, 복사 이유, 수정 사항, 직접/간접 의존성, 재사용 권한을 이 문서에 기록한다. 루트 LICENSE는 이번 검색에서 발견하지 못했으므로 공개 배포 권한을 추정하지 않는다.

## 첫 구현 제안 — 사용자 이해 확인 후 착수

빌드 기준은 x64 / C++20 / VS2022 v143이다. 프로젝트 형식은 사용자 요청에 따라 일반 Visual Studio 솔루션(.sln/.vcxproj)으로 확정했다. 패키지 버전은 실제 도입 단계에서 설명하고 선택한다.

우선 실행 앱, Win32 창, DX11 렌더링 책임만 분리한다. 실행 앱은 초기화 순서와 루프, 창은 HWND와 메시지, DX11 클래스는 장치·컨텍스트·스왑체인·RTV를 소유한다. 씬, ECS, 공통 RHI, 에디터 구조는 후속 요구가 확인될 때 결정한다.

1. Win32 창: 유니코드 창 생성, 메시지 처리, 종료, 최소화·복원 처리.
2. DX11: 장치와 컨텍스트 생성 → 스왑체인 생성 → 백버퍼 RTV 생성. 초기화 실패는 호출명과 HRESULT를 남긴다.
3. 프레임: 렌더 타깃 바인딩 → 지정 색으로 Clear → Present. 첫 단계는 VSync 사용을 제안한다.
4. 안정성: 리사이즈 전에 백버퍼 바인딩과 참조를 해제하고 재생성한다. 최소화·0 크기에서는 렌더링을 멈춘다. Present 실패와 장치 제거 이유를 기록한다.
5. 디버깅: Debug에서 디버그 레이어, 오류·손상 메시지 중단 설정, 객체 이름, 종료 시 Live Object 보고를 확인한다. 디버그 레이어가 없을 때 조용히 무시하지 않고 원인을 표시한다.

완료 기준: Debug/Release x64 빌드 성공, 색상 화면 표시, 창 크기 변경·최소화·복원·닫기 정상 동작, Debug 실행에서 오류 메시지와 자식 COM 객체 누수 없음. 현재 이 검증은 미실시다.

## DirectXTK 및 후속 입력

DirectXTK는 DX11용 보조 라이브러리이며 Keyboard, Mouse, 텍스처 로더, SpriteBatch 등을 제공한다. 공식 README 기준 VS2022/2026 및 Windows SDK 22000 이상을 지원한다. 현재 설치 SDK는 해당 범위에 있다. 출처: https://github.com/microsoft/DirectXTK

화면 색 지우기는 Win32·DX11 API로 구현하고, 입력 단계에서 DirectXTK Keyboard/Mouse를 엔진 Input 내부에 넣는다. 게임은 엔진 API로 눌림·유지·해제와 마우스 델타를 조회한다. DirectXTK 형식을 게임 API에 노출하지 않는다. 패키지는 새 엔진에서 독립적으로 설치하고 버전을 고정하며 SherlockEngine의 설치 캐시에 의존하지 않는다.

ImGui 도입 때에는 입력 상태 공급과 게임 입력 사용 여부를 분리하여 UI 조작·Alt-Tab 시 키 고착을 예방한다. ImGuizmo는 편집기 변환 조작이 필요해지는 단계에서 추가한다.

## 남은 결정

- Win32 단계 완료 설명 후 DX11 초기화 단계로 진행할지 확인.
- 팀원 도구 버전과 DirectXTK 설치 방식.
- 원격 저장소 공개 여부, 기존 에셋 추적/LFS·배포 권한.
- 보스전의 이동·전투·페이즈 전환을 검증할 최소 게임 흐름과 중간 일정. 현재 전체 일정을 확정하지 않음.

## 1단계 구현 기록 — Win32 창 (2026-10-01)

사용자가 준비 결과에 동의하고 저장소 연결 및 창 생성 구현 진행을 요청했다. origin을 `https://github.com/jung-seung-hwan/Nereides-Engine.git`로 등록했다. 원격 HEAD 조회 성공, 커밋 반환 없음. 공개 설정 변경이나 push는 하지 않았다.

처음에는 CMake와 VS2022 / x64 preset으로 구현했다. 이후 아래 전환 기록과 같이 일반 VS2022 솔루션으로 변경했다. C++20, Unicode, /W4, /permissive-, /utf-8 설정은 유지한다. 초기 빌드에서 MSVC 19.44.35225.0 / Windows SDK 10.0.26100.0을 확인했다.

- `src/Main.cpp`: wWinMain 진입점, 창 객체의 수명과 실행 결과 관리.
- `src/Platform/Win32Window.h`, `.cpp`: 창 클래스 등록, 클라이언트 1280×720 창 생성, GetMessage 루프, 크기·최소화 상태, 닫기, 창과 클래스 해제.
- HWND별 GWLP_USERDATA에 객체를 연결하여 메시지를 해당 창 객체에 전달한다. WM_NCDESTROY에서 연결을 제거한다.
- 현재 렌더링이 없으므로 GetMessage로 대기한다. DX11 단계에서 프레임 렌더링을 위한 루프로 바꾼다.
- 오류는 호출명·Win32 코드·시스템 설명을 VS 출력 창과 오류 메시지 창에 남긴다.
- `--smoke-test`는 숨긴 창을 생성하고 타이머로 WM_CLOSE를 전달하여 자동 종료한다.

SherlockEngine 코드는 복사하지 않았다. 창·메시지 처리의 책임 분리와 초기화/종료 흐름만 참고했다. 현재 외부 의존성은 Windows SDK와 user32이며 DirectXTK/ImGui/ImGuizmo는 아직 설치하거나 링크하지 않았다.

검증 결과:

| 검증 | 결과 |
| --- | --- |
| Debug / Release x64 빌드 | 둘 다 성공, 빌드 출력에 경고 없음 |
| Debug / Release 자동 종료 | 둘 다 exit code 0 |
| 초기 클라이언트 영역 | Win32 GetClientRect로 1280×720 확인 |
| 창 크기 변경 | 외부 창 크기 900×600 변경 후 클라이언트 884×561 확인 |
| 최소화 / 복원 | IsIconic으로 상태 전환 확인 |
| WM_CLOSE | 정상 종료, exit code 0 |

런타임 검증은 Win32 API와 프로세스 결과로 수행했다. Alt+F4 실키 입력, 화면의 육안 검증, 다중 DPI 모니터 이동은 미검증이다. 기본 DefWindowProc 경로를 유지하므로 시스템 키 처리를 방해하지 않는 구조다. DX11/GPU 검증은 아직 대상이 아니다.

샌드박스에서 .git/config 쓰기와 사용자 Windows SDK 탐색이 제한되어 해당 명령은 승인된 일반 사용자 실행으로 수행했다. 설치나 전역 Git 설정 변경은 하지 않았다.

다음 단계는 DX11 장치·컨텍스트·스왑체인·RTV 소유 클래스를 추가하고 Win32 HWND를 전달하는 것이다. 그때 HRESULT 진단, 디버그 레이어, 크기 변경 시 백버퍼 재생성을 설명하고 구현한다.

## 빌드 방식 변경 — VS2022 솔루션 (2026-10-01)

사용자가 Windows/VS2022 중심의 개발에서는 CMake의 추가 절차가 불필요하다고 판단하여 솔루션 방식으로 전환을 요청했다.

- 루트에 NereidesEngine.sln, NereidesSandbox.vcxproj, NereidesSandbox.vcxproj.filters를 추가했다.
- CMakeLists.txt와 CMakePresets.json을 제거했다. 이후 사용자 요청에 따라 이전 build/ 산출물(ALL_BUILD, ZERO_CHECK, CMakeFiles 등)도 정리했다.
- Win32 소스는 변경하지 않았다. Debug/Release x64, v143, C++20, Unicode, user32 링크와 기존 경고 옵션을 프로젝트 속성에 옮겼다.
- Debug는 최적화를 끄고 디버그 CRT를 사용한다. Release는 최적화와 일반 CRT를 사용하며 진단을 위한 PDB를 생성한다.
- 실행 파일은 bin/x64/구성, 중간 파일은 obj/NereidesSandbox/x64/구성에 생성한다. 팀원별 절대 경로를 프로젝트에 넣지 않았다.
- VS2022 MSBuild로 Debug/Release x64 빌드 성공, 두 실행 파일의 --smoke-test 정상 종료(exit 0)를 확인했다.

팀의 기본 작업 흐름은 루트 .sln 열기 → Debug/x64 선택 → F5 실행이다. 새 소스와 의존성 설정은 솔루션 탐색기 및 프로젝트 속성에서 관리한다. Visual Studio GUI에서 F5를 직접 누르는 검증은 수행하지 않았다.

## 2단계 — DX11 초기화·화면 색 지우기 (2026-10-01)

- Graphics/D3D11Renderer가 Device, Context, SwapChain, RTV를 ComPtr로 소유한다. Windows SDK의 d3d11/dxgi/dxguid를 링크하며 외부 패키지는 아직 없다.
- HW 장치 Feature Level 11_0, 2버퍼 FLIP_DISCARD 스왑체인, UNORM 백버퍼로 생성한다. 매 프레임 바인딩 → 짙은 청색 Clear → Present(1,0)로 표시한다.
- Main이 메시지와 프레임 루프를 관리한다. Win32Window는 PeekMessage 기반 ProcessMessages로 메시지를 처리한다. 최소화/0 크기는 WaitMessage로 대기한다.
- 크기가 달라지면 백버퍼 바인딩과 RTV 참조를 해제한 뒤 ResizeBuffers와 RTV 생성을 수행한다.
- HRESULT 실패를 VS 출력 창과 오류 창에 기록하며 장치 제거/리셋은 GetDeviceRemovedReason도 출력한다. 자동 장치 재생성은 아직 구현하지 않았다.
- Debug에서 디버그 레이어 및 ERROR/CORRUPTION 중단을 활성화한다. RTV 이름을 지정하고 종료 때 자식 자원을 먼저 해제한 뒤 Live Object 보고를 요청한다.
- Debug/Release x64 빌드와 HW GPU 자동 실행(exit 0) 성공. Debug에서 3가지 크기 변경, 최소화·복원 및 WM_CLOSE 종료(exit 0)를 확인했다.
- 화면 육안 확인과 디버그 출력 전체/Live Object 보고의 직접 판독은 아직 미실시다. 런타임 성공만으로 모든 경고와 누수가 없다고 단정하지 않는다.

SherlockEngine은 수정하지 않았으며 복사한 코드는 없다. 장치→어댑터→팩토리→스왑체인 흐름과 백버퍼 참조 해제 순서를 참고해 독립 구현했다. 공통 RHI·핸들 풀·ImGui 의존성은 도입하지 않았다.
