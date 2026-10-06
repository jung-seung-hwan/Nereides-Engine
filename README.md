# Nereides Engine

Windows / C++20 / DirectX 11 공통 엔진 기반과 편집용 샌드박스다. 입력·시간·객체·컴포넌트·FBX 로딩·애니메이션·충돌·최소 에디터·씬 저장·2D 전환 서비스를 구현했다. 현재 실행 장면은 임시 도형과 연결 검증 예제이며 완성된 보스전은 아니다.

## 어디서부터 읽을까

- [단계별 구현 로드맵](docs/implementation-roadmap.md): 진행 상태와 각 단계의 읽기 순서·실행·검증 기록.
- [엔진 구현 범위](docs/engine-scope.md): 기획 기준, 엔진·렌더·콘텐츠 담당 경계.
- [콘텐츠와 렌더 연결 안내](docs/integration-contracts.md): 현재 API·수명·프레임 순서와 남은 통합.
- [초기 구현 기록](docs/technical-review.md): 창·DX11·첫 삼각형의 개발 이력.

## 최초 준비

VS2022의 C++ 데스크톱 개발 도구(v143)와 Windows SDK를 준비한다. 프로젝트는 일반 Visual Studio 솔루션을 사용한다. Debug 실행에는 Windows Graphics Tools의 DX11 디버그 레이어가 필요하다.

자신의 vcpkg 설치 경로를 전달해 의존성을 설치한다.

```powershell
./scripts/setup-dependencies.ps1 -VcpkgRoot C:/vcpkg
```

vcpkg.json baseline과 triplets/x64-windows-v143.cmake로 Assimp·ImGui·ImGuizmo·nlohmann-json을 고정한다. 설치 결과는 프로젝트의 vcpkg_installed에 있고 Git에서 제외한다. DLL과 라이선스 고지는 빌드 출력에 복사한다. 다른 엔진의 설치 캐시에 의존하지 않는다.

## 빌드와 실행

1. NereidesEngine.sln을 VS2022에서 연다.
2. Debug 또는 Release / x64를 선택한다.
3. NereidesSandbox를 시작 프로젝트로 두고 F5를 누른다.

처음에는 Edit 모드로 시작한다. Assets의 폴더 트리·검색·형식 필터로 모델을 찾고, 선택 후 Place in Scene으로 배치한다. 더블클릭과 Scene으로 드래그하는 방식도 지원한다. Browse...로 외부 파일을 선택한 뒤 Place in Scene을 누를 수 있다. Scene 위에서 우클릭 드래그는 회전, 가운데 버튼은 화면 이동, 휠은 확대·축소, F는 선택 객체에 초점 맞추기다. W/E/R로 이동·회전·크기 도구를 고르고 Inspector의 XYZ 값을 편집한다.

Scene과 Assets 사이의 경계를 끌어 목록 높이를 조절한다. 로그는 상단 Console 버튼으로 별도 창에서 확인하고, 작업 실패 시 View log로 해당 오류에 이동한다. [Assets·Console 사용 안내](docs/stages/11-assets-console.md)에 조작과 구현 구조를 정리했다.

Ctrl+Z / Ctrl+Y로 추가·삭제·속성 수정을 복구하고, Ctrl+S 또는 Save로 저장한다. 첫 저장은 파일을 선택하며 Open으로 다시 불러온다. Play에서 예제 동작을 시험하고 Stop / restore로 편집 상태에 돌아온다. 게임 실행 중 Scene 위에서 WASD로 예제 상자를 이동한다. 자세한 순서와 한계는 [팀원용 에디터 사용 안내](docs/stages/10-editor-workflow.md)에 기록했다.

실행 파일 위치에서 프로젝트 루트 또는 배포 data 폴더를 찾아 상대 경로 기준을 맞춘다. MMD와 assets는 각 팀원이 별도로 준비한다. 임시 도형과 내장 전환 미리보기는 외부 에셋 없이 실행된다.

## 검증

```powershell
./scripts/verify.ps1
```

Debug/Release 빌드 후 다음 모드를 실행하며, 실패한 종료 코드나 20초 시간 초과를 오류로 처리한다.

| 실행 인자 | 확인하는 것 |
| --- | --- |
| --engine-tests | 입력·시간·객체·리소스·재생·충돌·저장·전환·이벤트·반복 정리 |
| --smoke-test | 최초 삼각형과 배경의 GPU 픽셀 |
| --scene-smoke-test | 월드/카메라/깊이를 사용하는 3D 출력 |
| --editor-smoke-test | 에디터와 장면 출력, captures/editor.bmp |
| --editor-workflow-test | 실제 UI 입력으로 배치·Z 편집·Undo/Redo·저장·Play/Stop·폴더 검색·Console·실패 보존 |
| --editor-resize-smoke-test | Scene 뷰포트를 포함한 900×600 / 1280×720 크기 변경 |
| --presentation-smoke-test | 2D 미리보기와 전투 정지, captures/presentation.bmp |
| --resize-smoke-test | 실제 창 크기 변경 후 백버퍼·깊이 버퍼·카메라·픽셀 |
| --benchmark | 임시 장면 1280×720, 30프레임 예열 후 180프레임 시간·메모리 기록 |
| --model-test 경로 | 로컬 모델/클립, 뼈 행렬·GPU 표시 변화·WIC 업로드 확인 |

로그는 logs/engine.log다. benchmark의 시간은 Present/VSync를 포함한 프레임 경과 시간이며 GPU 단독 측정이나 최종 FHD 60FPS 합격 근거가 아니다. 외부 FBX 시험은 각자의 assets가 필요하다.

Release 검증 후 `./scripts/package.ps1`로 새 dist 폴더에 실행 파일·DLL·고지·data를 복사한다. 외부 모델은 자동 배포하지 않는다. 새 PC에는 해당 MSVC 런타임이 필요하며 현재 패키지 검증은 이 개발 PC에서 수행했다.

## 현재 한계와 다음 통합

렌더러는 기본색과 스키닝을 확인하는 임시 백엔드다. Sherlock 포팅·최종 재질/텍스처·그림자·바다/폭풍은 렌더 담당과 연결한다. 독립 Scene 뷰포트와 Undo/Redo는 지원한다. 별도 모션 리타게팅·고급 재질 도구·자동 저장·다중 선택·오디오는 후속 작업이다.

플레이어/보스 규칙과 실제 HUD·세 구역 완주는 콘텐츠 통합이 필요하다. 임시 예제 수치는 게임 밸런스 확정값이 아니다. 2026-11-23 시연, 12-16 내부 완료, 12-18 발표의 합격 범위는 구현 범위 문서를 따른다.
