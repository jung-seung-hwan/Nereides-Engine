# 02 객체 컴포넌트 변환 카메라

콘텐츠가 각 객체에 행동을 붙이고 같은 실행 루프에서 갱신하게 한다. `Scene`이 객체를, `Object`가 컴포넌트를 unique_ptr로 소유한다. 외부에는 재사용되지 않는 ObjectId를 넘기고 사용할 때 `Find`로 확인한다. 반환된 포인터는 소유권이 아니며 Flush/Clear 이후 보관하지 않는다.

## 읽는 순서

1. `src/Scene/Scene.h`: ObjectId → Transform → Component → Object → Scene → Camera.
2. `src/Scene/Scene.cpp`: Create/Find → SetParent/World → Destroy/Flush → Update.
3. `src/Sandbox/MoveComponent.h`: 콘텐츠가 상속하는 최소 행동 예제. WASD, 초당 3m 임시값. 게임의 완성된 플레이어 로직은 아니다.
4. `src/Core/Application.cpp`: 객체 생성·컴포넌트 부착·Update 호출. 현재 위치는 창 제목에 표시한다. 화면의 삼각형은 아직 객체의 위치와 연결하지 않았다.
5. `src/Tests/EngineTests.cpp`의 TestScene: 부모 변환·순환 거부·갱신 중 삭제·생성·Clear·컴포넌트 제거·카메라를 검증한다.

## 수명과 좌표 계약

Destroy는 해당 객체와 자손을 즉시 조회/갱신 대상에서 제외한다. 실제 메모리는 갱신이 끝난 Flush에서 해제한다. 갱신 중 Clear도 같은 원칙을 따른다. Update에서 생성된 객체는 다음 프레임부터 실행한다. 컴포넌트 Remove도 비활성화 후 Flush에서 해제하며, 자기 자신을 지우는 동작이 안전해야 한다.

부모를 변경할 때 로컬 Transform을 유지한다. 월드 위치 보존 리패런팅은 아직 없다. 부모가 비활성이면 자식 행동도 실행하지 않는다. 좌표는 +Y 위·+Z 앞, 미터·라디안, 행 벡터이며 월드 행렬은 local × parent다. Camera는 월드 역행렬과 왼손 투영을 만든다. 이 규격은 렌더 담당 연결 시 확인할 구현 기준이다.

## 실행과 검증 결과

F5 → WASD → 창 제목의 x 변화 확인. Esc 정지 중 이동량은 0이다. `scripts/verify.ps1`로 Debug/Release 빌드와 엔진 테스트·GPU smoke-test를 실행한다.

2026-10-06 두 구성의 빌드·엔진 테스트·기존 GPU 픽셀 검사 모두 통과했다. 객체 생성/삭제 예제와 카메라 수학은 구현했지만 장면의 3D 표시는 다음 단계다. 충돌·부모 보존 직렬화·전투 상태는 이 단계의 완료 주장에 포함하지 않는다.
