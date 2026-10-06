# 06 최소 에디터와 씬 저장

이 문서는 06단계 당시의 기록이다. 현재 화면·조작·복구·저장은 [10단계 에디터 안내](10-editor-workflow.md)를 따른다. 독립 뷰포트와 Undo/Redo는 후속 단계에서 구현했다.

콘텐츠 담당이 코드 수정 없이 객체를 배치하고 기존 숫자 설정을 조절할 수 있게 했다. ImGui 창에서 객체 선택, 부모 확인/변경, Transform·이동 속도·판정·색·범위가 있는 숫자 매개변수를 편집한다. ImGuizmo와 Inspector는 같은 Transform을 변경한다.

## 읽는 순서

1. `src/Editor/Editor.h/.cpp`: Initialize → Begin → DrawDebug → Render. Begin은 편집 UI, DrawDebug는 갱신된 실제 충돌 형태, Render는 DX11 출력이다.
2. `src/Data/SceneIO.h/.cpp`: schema version 1, 객체 ID 재매핑, 부모 복원, 컴포넌트와 카메라 설정 복원. 별도 Scene을 전부 구성한 뒤 성공했을 때만 교체한다.
3. `src/Core/Application.cpp`: UI 입력 분리 → 시간·장면 갱신 → 실제 판정 표시 → 렌더 입력. 로드 뒤 이전 입력과 접촉 기록을 제거한다.
4. `src/Tests/EngineTests.cpp`의 TestSceneIO: 저장/로드, 부모/설정 복원, 기존 파일 교체, 잘못된 버전의 실패와 기존 장면 유지.

## 사용

F5로 실행한다. 왼쪽 Hierarchy에서 선택하고 Position/Rotation/Scale을 수정하거나 Move/Rotate/Scale tool을 사용한다. Add cube로 도형을 추가하고 Delete로 제거한다. 카메라와 예제 플레이어는 실수로 지우지 않도록 보호한다.

Model path에 FBX 경로를 넣고 Import model을 누른다. 모델 선택 시 클립 이름 버튼으로 재생한다. 경로 오류는 상태와 로그에 남긴다. 이동 속도는 Example player에서, 예제 전투 매개변수는 Contact example에서 편집한다. 이 숫자는 콘텐츠가 조회할 설정 예제이며 실제 약점 로직이 구현된 것은 아니다.

Save scene은 기본 `data/scenes/sandbox.json`에 저장한다. Load scene은 파일을 검사해 새 장면으로 교체한다. 미저장 수정이 있으면 에디터 안에서 교체 여부를 확인한다. 파일 교체는 임시 파일 작성 후 원자적 교체를 사용한다. 씬 배치·설정을 저장하며 현재 전투 진행·재생 시각은 저장하지 않는다.

## 직렬화 계약과 한계

현재 등록된 컴포넌트는 내장 cube·ModelComponent·MoveComponent·Collider·Parameters다. 임의 MeshComponent를 cube로 조용히 바꾸지 않으며 등록되지 않은 타입·중복 타입은 저장 오류다. 콘텐츠의 새 행동은 명시적인 저장/복원 연결을 추가해야 한다. 모델 경로는 가능한 한 프로젝트 작업 경로 기준 상대 경로로 기록한다. 같은 프로젝트 루트에서 실행한다.

뷰포트는 현재 전체 창이며 에디터는 그 위에 표시한다. 독립 오프스크린 뷰포트는 렌더 포팅 때 연결할 항목이다. Undo/Redo·고급 재질 편집·텍스처/PBR 출력은 아직 없다. 기즈모는 양수 스케일의 TRS를 기준으로 하며 비균일 부모 변환의 shear 보존을 보장하지 않는다. 11월 시연의 실제 전투 사이클/HUD는 유상준·김진우의 콘텐츠 통합 항목으로 남는다.

## 검증 결과

2026-10-06 Debug/Release 빌드, 엔진 테스트, 삼각형/장면/에디터 GPU smoke-test 통과. 에디터가 렌더한 캡처에서 객체 목록·Inspector·기즈모·실제 충돌선·저장/로드 버튼과 3D 장면이 함께 표시됨을 확인했다. 모든 마우스 조작의 자동 회귀를 완료한 것은 아니다. `--editor-smoke-test`는 자동 종료와 함께 `captures/editor.bmp`를 생성한다.
