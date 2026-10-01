# Nereides Engine

Windows / C++ / Direct3D 11 기반의 새 엔진. Win32 창 생성과 DX11 초기화·화면 색 지우기를 구현하고 Debug/Release x64 실행을 검증했다.

- 실제 작업 경로: `C:\Users\User\Desktop\Nereides Engine` (사용자 확인 완료)
- 참고 전용: `C:\Users\User\Documents\GitHub\SherlockEngine`
- 첫 목표: Win32 창 → DX11 초기화 → 화면 색 지우기
- DirectXTK 활용, 입력은 엔진 Input 인터페이스로 제공
- 디버깅을 우선하며 ImGui / ImGuizmo는 이후 단계에서 도입
- 최종 발표: 2026-12-18 (Asia/Seoul)

초기 조사와 다음 단계 제안은 [기술검토서](docs/technical-review.md), 협업 규칙은 [협업 안내](docs/collaboration.md)를 참고한다.

기존 `기획`, `이미지 레퍼런스`, `MMD` 폴더는 보존한다. `MMD`는 Git에서 제외하고 각 팀원이 로컬에 별도로 준비한다. origin은 https://github.com/jung-seung-hwan/Nereides-Engine.git 이다. 공개 여부, 나머지 에셋 배포 권한, Git LFS 정책은 아직 결정하지 않았다.

## 빌드 및 실행

VS2022의 C++를 사용한 데스크톱 개발 도구(v143)와 Windows SDK를 준비한다. CMake는 사용하지 않는다.

1. 루트의 `NereidesEngine.sln`을 Visual Studio 2022로 연다.
2. 상단 구성을 `Debug`, 플랫폼을 `x64`로 선택한다.
3. `NereidesSandbox`를 시작 프로젝트로 설정한다(필요한 경우 프로젝트 우클릭).
4. F5로 빌드·디버깅하거나 Ctrl+F5로 디버거 없이 실행한다.

소스는 솔루션 탐색기에서 추가하고 빌드 옵션은 프로젝트 속성에서 관리한다. `.sln`, `.vcxproj`, `.vcxproj.filters`는 Git에 공유하고 `.vcxproj.user`는 공유하지 않는다.

개발자 PowerShell에서 명령으로 빌드할 수도 있다.

```powershell
msbuild .\NereidesEngine.sln /p:Configuration=Debug /p:Platform=x64
msbuild .\NereidesEngine.sln /p:Configuration=Release /p:Platform=x64
& '.\bin\x64\Debug\NereidesSandbox.exe'
```

`--smoke-test`를 전달하면 숨긴 창에서 DX11 초기화·렌더링 후 약 0.5초 뒤 정상 종료한다. 일반 실행은 1280×720 클라이언트 영역에 짙은 청색 화면을 표시한다. VS 디버거 출력 창에서 창 메시지, DX11 오류, Debug Layer와 종료 시 Live Object 보고를 확인할 수 있다. Debug 실행에는 Windows Graphics Tools의 DX11 디버그 레이어가 필요하다.

실행 파일은 `bin/x64/Debug` 또는 `bin/x64/Release`, 중간 파일은 `obj`에 생성되며 Git에서 제외한다. 이전 CMake 방식의 `build` 폴더는 정리했다.
