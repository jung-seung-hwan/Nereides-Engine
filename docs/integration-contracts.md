# 콘텐츠와 렌더 연결 안내

2026-10-13 라이프 사이클·컴포넌트 등록과 편집 변경까지 반영한 공통 기반의 실제 API다. [전체 구조 HTML](engine-architecture.html)에서 시스템 연결을 볼 수 있다. 단계별 구현 이유는 [로드맵](implementation-roadmap.md)을 읽는다. 이 문서의 임시 규격은 팀 통합에서 검증하며 원문에서 미정인 게임 수치를 확정하지 않는다.

## 객체와 프레임

`Scene::Create`가 객체를 소유하고 `Object::Add<T>`가 행동을 붙인다. 사용자 코드는 ObjectId를 보관하고 `Scene::Find`로 유효성을 확인한다. 객체/컴포넌트 포인터는 소유권이 없고 삭제 Flush 이후 보관하면 안 된다. 갱신 중 생성·부착한 대상은 현재 호출의 목록에 포함되지 않으며 다음 갱신 호출부터 참여한다. 제거는 즉시 비활성화하고, 메모리 해제는 각 경로의 Flush에서 수행한다. `Scene::FixedUpdate(context, false)`가 이 경계를 사용하며 기본 인자를 생략한 직접 호출은 기존처럼 Update 끝에서 Flush한다.

현재 전역 갱신 단계는 Logic → Animation → Late다. 콘텐츠 행동은 기본 Logic, ModelComponent는 Animation, 추적 카메라 같은 후처리는 Phase를 Late로 지정한다. 생성 순서가 애니메이션을 행동보다 먼저 실행시키지 않도록 한다. Transform 월드 행렬은 조회 시 계산한다. 같은 단계의 순서는 객체/컴포넌트 등록 순서다.

그다음 CollisionWorld::Step → 콘텐츠 결과 해석 → 이벤트 전달 → Scene::Flush 순서로 한 tick을 끝낸다. 필요한 tick을 모두 실행한 뒤 Scene::Update → Scene::Flush → CollectRenderFrame → 렌더 순서로 연결한다. 샌드박스는 충돌을 로그로만 표시한다. 전투 결과 처리를 새 엔진 규칙으로 추가하지 말고 콘텐츠의 명시적인 처리 지점에서 수행한다.

## 실행 수명과 초기화 순서

객체·컴포넌트 추가와 설정·부모 관계 복원을 끝낸 뒤 `Scene::BeginPlay()`를 호출한다. 호출 시점의 목록에서 모든 `Awake` → 활성 대상의 `OnEnable` → 활성 대상의 최초 `Start` 순으로 처리한다. `Awake`는 비활성 대상에도 한 번 호출하지만 `Start`는 첫 활성 실행까지 미룬다. 같은 단계의 순서는 등록 순서이며 다른 컴포넌트의 Awake 결과는 Start에서 사용한다. 숫자 우선순위나 의존성 정렬은 제공하지 않는다.

`Scene::Update/FixedUpdate`는 BeginPlay 전에는 아무 게임 콜백도 실행하지 않는다. 각 갱신은 새 목록을 확보하고 새 컴포넌트의 초기화를 먼저 처리한다. 콜백 중 추가한 대상은 다음 갱신 호출에 참여한다. 따라서 FixedUpdate에서 생성한 대상은 같은 렌더 프레임의 Update에서 시작될 수 있다. 초기화 도중 추가한 대상 역시 다음 목록까지 기다린다.

활성 값은 `Enabled()`로 읽고 `SetEnabled(bool)`로 변경한다. false는 즉시 갱신·충돌 수집에서 제외한다. OnEnable은 다음 BeginPlay/갱신 시작 경계에서, OnDisable은 갱신 경계 또는 Flush에서 호출된다. 부모 비활성화도 자식의 실제 활성 상태에 반영한다. 경계 사이 false→true 변경은 최종 상태로 합쳐지며 매 대입마다 콜백을 호출하는 방식이 아니다. 재활성화는 OnEnable만 반복하고 Awake/Start는 반복하지 않는다.

Remove/Destroy는 예약이며 Flush에서 OnDisable(필요한 경우) → OnDestroy → 메모리 해제를 수행한다. OnDestroy는 Awake를 시작한 컴포넌트에만 한 번 호출한다. 편집 중 삭제나 초기화 전에 삭제된 대상은 C++ 소멸자만 호출한다. `OnDisable/OnDestroy`는 noexcept이므로 외부 등록·구독을 정리하되 예외를 전파하지 않는다. 삭제 콜백에서는 새 게임 객체를 생성하지 않는다.

`EndPlay()`는 실행 상태를 끝내고 모든 OnDisable 후 모든 OnDestroy를 호출한 뒤 씬을 비운다. 콜백 안에서 요청하면 메모리 해제는 현재 호출 경계까지 미룬다. 에디터 Stop은 실행 씬을 정리한 다음 저장했던 편집 스냅샷으로 교체한다. 다음 Play는 새 컴포넌트 인스턴스로 시작한다. 초기화/갱신 예외도 실행 씬 정리 후 호출자에게 전달한다. 에디터의 Play 초기화 실패는 편집 스냅샷을 복원한다.

씬 소멸자와 이동 대입도 기존 대상의 정리를 보장한다. 실행 중인 씬을 이동 원본으로 사용하는 것은 금지한다. 정리 콜백에서 사용하는 Events 등 서비스는 Scene보다 오래 살아 있어야 한다. Application은 소멸자 본문에서 EndPlay를 먼저 호출한다. 이벤트 구독자는 OnEnable에서 Subscribe하고 OnDisable에서 Unsubscribe하는 방식으로 수명을 연결한다.

실행 수명 플래그는 저장하지 않는다. 등록한 설정값은 아래의 명시적 리플렉션을 통해 편집·저장한다.

## 컴포넌트 등록과 속성 편집

`Reflection/ComponentRegistry.h`의 `DescribeComponent<T>`로 영구 타입 이름·표시 이름·생성 함수를 정의하고, `Property`에 멤버 포인터와 범위를 전달한다. `ComponentTypes().Register(...)`는 에디터 사용·씬 로드 전에 호출한다. 기본 생성 가능한 Component 파생형을 대상으로 하며 생성자는 설정값 준비만 수행한다. 등록 기본값 확인 과정에서도 인스턴스가 생성되므로 이벤트 구독 등 실행 작업은 라이프사이클에 둔다. 레지스트리는 실행 스레드에서 사용하며 병렬 등록은 지원하지 않는다.

```cpp
ComponentTypes().Register(DescribeComponent<MyComponent>("my-component", "My Component", {
    Property("speed", "Speed", &MyComponent::speed, 0, 1000)
}));
```

현재 공통 속성은 `float`, `int`, `bool`, `DirectX::XMFLOAT3`를 지원한다. 등록된 속성 모두 편집·저장 대상이며 포인터·구독 ID·공격 진행 상태는 등록하지 않는다. 에디터 Add Component 목록·Inspector와 SceneIO가 같은 메타데이터를 사용한다. 숫자 범위·유한성·자료형을 검사하며, 새 필드가 과거 파일에 없으면 등록 기본값을 적용한다. 타입/필드 저장 키를 바꿀 때는 별도 파일 마이그레이션이 필요하다.

기본 등록은 `ComponentRegistry.cpp`의 MoveComponent(speed 0~1000)다. 에디터에서 추가·삭제·활성 설정·속도 편집·Undo/Redo·저장·복원할 수 있다. 등록 기반 추가는 같은 타입의 중복을 막는다. 기존 cube/model/collider/parameters는 전용 편집·직렬화 경로를 유지한다. 모델은 Assets에서 배치한다. 리소스 선택·열거형·객체 참조·배열·중첩 속성과 C++ 소스 편집/핫리로드는 미구현이다.

## 프레임 갱신과 고정 갱신

`Component::FixedUpdate`는 50Hz로 호출한다. 기존 MoveComponent의 이동과 ModelComponent의 전투 애니메이션은 이 경로로 옮겼다. `Component::Update`는 필요한 tick을 모두 처리한 뒤 렌더 프레임마다 한 번 호출한다. 두 경로 모두 Logic → Animation → Late 순서다. Phase가 Late여도 FixedUpdate는 각 tick 안에서, Update는 프레임 갱신 안에서 실행된다.

Play 중 정지·전환·포커스 상실 때에도 프레임 Update는 호출한다. 최소화 대기 중과 Edit 모드에는 호출하지 않는다. 시각 연출은 `time.presentation.delta`, 정지와 무관한 실제 시간 처리는 `time.real.delta`를 사용한다. Update에 전달하는 `Time::FrameView()`는 combat/escape의 누적값을 유지하고 delta만 0으로 만든다. FixedUpdate는 combat/escape delta를 사용한다.

Update의 입력은 해당 렌더 프레임 입력, FixedUpdate의 입력은 ConsumeTick이 제공하는 보관 입력이다. 같은 키가 각 경로에 한 번씩 보일 수 있으므로 동일 행동을 양쪽에서 처리하지 않는다. Update에서 발생시킨 게임 이벤트는 다음 고정 tick의 Dispatch에서 처리한다. 정지 중 즉시 필요한 UI 작업을 이 큐에 의존시키지 않는다.

생성 대상은 현재 호출의 스냅샷에 포함되지 않는다. FixedUpdate에서 생성하면 다음 FixedUpdate부터 고정 갱신을 받지만 같은 프레임의 후속 Update에는 참여할 수 있다. 프레임 Update에서 삭제한 대상은 Update 끝에서 Flush하며 렌더에서 제외된다. 고정 갱신 삭제는 충돌·결과·이벤트 뒤 Flush한다. 콜백에서 Flush/Clear로 외부 포인터 수명을 앞당기지 않는다.

## 입력과 시간

일반 실행은 `FixedStep::DefaultHz = 50.0`으로 시작해 0.02초마다 게임을 갱신한다. 변경 시 이 기본값을 수정하거나 생성자에 Hz를 전달한다. UI 설정·실행 중 변경은 아직 없다. 프레임당 최대 8회만 따라잡고 초과한 완전한 tick 시간은 버린다. 소수 잔여 시간은 유지한다. 이는 지연 때 게임 시계가 실제 시간보다 느려질 수 있는 초기 보호 정책이다. 일시정지·포커스 상실·최소화·Play/Stop·씬 교체·전환에서는 누적을 초기화하고 재개 프레임의 delta를 제외한다.

`Input::BeginFrame`은 UI용 프레임 입력만 초기화한다. `ConsumeTick`은 보관된 눌림·해제·마우스 변화량을 다음 tick에 한 번 전달한다. Held는 후속 tick에도 유지한다. tick이 없는 프레임에서도 입력은 남으며, 같은 키의 여러 눌림은 bool 하나로 합친다. 시각별 입력 재현이나 콤보 예약 큐는 아니다.

`AdvanceFrame`은 real/presentation과 frame을, `AdvanceTick`은 combat/escape와 tick을 갱신한다. 기본 combat delta는 0.02 × 전투 배속, escape delta는 0.02다. Unity의 전역 timeScale과 완전히 같은 규칙은 아니며 기존 분리 시계 정책을 유지한다. 렌더 보간은 없다.

`FrameContext`에서 Input과 Time을 읽는다. Pressed/Held/Released는 Windows 가상 키 값 기준이다. 이벤트 공급은 Win32, 마우스 상대 이동은 Raw Input이다. UI에 입력을 넘기거나 전환·재시작할 때 DiscardHeld/Capture로 이전 입력을 제거한다. 콘텐츠의 예약 입력은 별도로 정리해야 한다.

Time의 combat은 배율 적용, escape는 배율 미적용, presentation은 별도 연출 시간이다. paused는 게임 시계를 멈추며 pausePresentation은 연출 정지를 정한다. combatStopped는 구역 진입 후 연출 구간에 사용한다. 탈출 구역에 들어가기 전의 전투를 멈추는 용도로 쓰지 않는다.

## 모델과 공격

ModelCache::Load → Object::Add<ModelComponent> → player.Play(clipIndex, loop, blendSeconds) 순으로 사용한다. 모델/클립은 immutable 공유 데이터이며 애니메이션 시간·포즈는 객체별로 가진다. 현재 구현은 모델과 클립이 함께 들어 있는 FBX를 대표 검증했다. 별도 모션 파일 리타게팅은 후속 작업이다.

공격 식별자는 콘텐츠가 공격마다 새로 부여한다. Timeline으로 시작/판정 활성/종료 마커를 받고 같은 공격 시각으로 EvaluateTrajectory를 평가한다. 긴 프레임에서 여러 마커를 통과하면 모두 처리해야 한다. 곡선의 연속 판정은 중간 시점의 샘플을 제공한다. Collider.attack에는 이 식별자를, layer/mask에는 대상을 설정한다.

CollisionWorld는 후보를 반환한다. HitLedger는 콘텐츠가 적중을 수락한 뒤에만 Record한다. 패링/무적/공격체 파괴 우선순위를 판단하기 전에 후보부터 중복 소모시키지 않는다. Contact.fraction을 사용하면 전투 시각으로 얻은 후보를 해당 tick의 탈출 시각에 대응시킬 수 있다. 현재 position은 접촉 당시 첫 객체 중심 근사값이며 정밀한 접촉 법선은 제공하지 않는다.

## 렌더 포팅

RenderFrame은 CPU 메시 공유 참조·월드/뷰/투영·기본색·스키닝 행렬·시간을 제공한다. 모델 정점/인덱스는 변경 없는 리소스다. 지금의 D3D11Renderer는 unlit 검증 백엔드이며 Scene이나 콘텐츠 클래스를 직접 읽지 않는다. Sherlock의 Renderer/BuildDrawList 입력을 이 경계에 맞춰 바꾸고, 광원·환경·최종 재질·그림자용 입력은 렌더 담당과 추가한다.

좌표는 +Y 위·+Z 앞, 미터·라디안, row vector·local × parent다. HLSL은 row_major이고 WVP는 world × view × projection이다. 뼈 행렬은 mesh inverse bind × animated global이다. 한 메시당 최대 127개 뼈와 fallback 한 개를 현재 검증 백엔드에서 지원한다. 메인/그림자 패스가 같은 포즈를 사용하도록 포팅 시 확인한다.

장치·스왑체인·GPU 캐시는 렌더러 하나가 소유한다. UI는 그 장치/컨텍스트를 빌려 쓴다. CPU 메시의 마지막 참조가 사라지면 다음 프레임에서 해당 GPU 캐시를 정리한다. 변경은 기존 메시를 수정하지 말고 새 immutable 데이터로 교체한다.

## 저장과 재시작

에디터는 09~10단계부터 Edit/Play를 분리한다. EditHistory는 SceneIO::Encode/Decode의 메모리 스냅샷을 사용하며 Undo/Redo/Stop 때 새 Scene으로 교체한다. 교체 후 카메라·플레이어 역할 ID와 선택 참조를 다시 얻고 충돌·입력을 초기화한다. 저장 ID는 파일 안의 정규화된 번호이며 런타임 ID를 영구 식별자로 취급하지 않는다. 새 콘텐츠 컴포넌트를 연결할 때에도 직렬화 등록이 필요하다. 등록하지 않은 컴포넌트를 조용히 누락시키지 않고 오류로 보고한다.

편집 카메라는 Scene 객체가 아니며 저장되지 않는다. 에디터가 RenderFrame의 view/projection을 편집 뷰포트에 맞게 바꾼다. Play에서는 게임 카메라를 쓰고 Stop은 실행 직전 편집 상태를 복원한다. Scene 렌더 타깃의 크기는 창의 백버퍼와 별개다.

SceneIO는 배치·구성만 저장한다. 새 콘텐츠 컴포넌트는 지원하는 설정값을 ComponentRegistry에 등록하면 공통 경로로 저장된다. 리소스·동적 구조처럼 공통 속성이 지원하지 않는 데이터는 전용 직렬화 또는 별도 콘텐츠 설정 파일이 필요하다. Parameters의 최소/최대 범위는 콘텐츠에서 정의하고 Inspector는 그 범위를 따른다. 현재 전투 HP·Stagger·입력 예약을 씬 저장에 포함하지 않는다.

권장 재시작 순서는 입력/공격 중단 → Events::Reset → CollisionWorld::Reset → Transition::Reset → 이전 Scene 정리 → 새 객체와 구독 구성이다. 이전 epoch의 이벤트나 token의 완료 신호는 거부된다. 플레이어·보스 HP/게이지/쿨다운 초기값은 콘텐츠가 명시적으로 설정한다. 재시작을 1번 배로 연결하는 정책도 보스 콘텐츠가 소유한다.

## 편집 로그 연결

11단계부터 `Log::Write(level, message)`는 실행 내 단조 증가 ID를 반환한다. 기존 호출부는 반환값을 무시해도 된다. `Log::Entries()`는 최근 1,000건의 시각·심각도·메시지·ID 스냅샷을 제공하며, Console은 이를 검색·그룹화해 표시한다. `Console::Reveal(id)`는 해당 작업 오류로 이동한다. Clear view는 UI 표시 경계만 바꾸며 로그 파일과 다른 소비자의 기록을 지우지 않는다. 파일에는 발생 건별로 로컬 시각을 포함해 기록한다.

## 실제 시연까지 남은 연결

- 플레이어 행동·콤보·회피·패링·강화·R·HUD와 타격 시각 검증.
- 공용 Stagger·약점 노출/파괴/복귀·공격체·동시 결과 우선순위.
- 세 구역 데이터·실제 준비 완료 조건·사망/실패/재시작 UI, 두 차례 전환.
- Sherlock 렌더 포팅·최종 재질/텍스처·조명/환경·그림자와 스키닝 동기화.
- 실제 캐릭터/촉수의 모든 모션·원본 외형 대조, 오디오 담당 및 연결.

각 항목은 엔진 기반 완료와 분리해 통합 합격을 기록한다. 현재 샌드박스의 도형과 전환 미리보기를 11월 핵심 전투 시연 또는 12월 완주 합격으로 취급하지 않는다.
