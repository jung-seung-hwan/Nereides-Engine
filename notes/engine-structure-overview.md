# Nereides Engine 전체 구조와 흐름

엔진 코드를 학습하면서 정리한 전체 구조입니다. 누가 무엇을 소유하는지, 한 프레임이 어떤 순서로 처리되는지, 모듈 사이에 무엇이 넘어가는지를 다룹니다.

- 기준 코드: `feat/Program_JSH` 브랜치, 커밋 `eefbdf9` (2026-10-30)
- 렌더러 내부(Renderer·RHI·Shaders)는 다루지 않습니다.

---

## 1. 누가 무엇을 소유하나

**모든 것의 주인은 `Application` 하나입니다.** 전역 싱글톤이 없습니다. 각 서비스는 `Application`의 멤버로 존재하고, 필요한 곳에 **참조로 빌려줍니다**.

```
wWinMain (Main.cpp)
 └─ Application                       ← 모든 서비스를 값 멤버로 소유
     ├─ Input           m_input       키·마우스 상태
     ├─ Time            m_time        시계 4개 (real / combat / escape / presentation)
     ├─ Win32Window     m_window      OS 창, 메시지 처리
     ├─ D3D11Renderer   m_renderer    GPU, 화면 출력
     ├─ Editor          m_editor      ImGui, 저장/로드, Play/Stop, 연출(Transition)
     │    ├─ ModelCache / TextureCache   (Resource)
     │    ├─ Transition                  (Presentation)
     │    └─ EditHistory, m_playSnapshot (편집 상태 문자열)
     ├─ Scene           m_scene       Object → Component 트리 (게임 세계 전체)
     ├─ ObjectId        m_player, m_camera
     ├─ Camera          m_cameraData  시야각·near·far
     ├─ CollisionWorld  m_collision   충돌 계산 (지난 tick의 접촉 기억)
     └─ Events          m_events      알림 대기열
```

멤버는 선언의 역순으로 해제됩니다. 그래서 Editor가 Renderer보다 먼저, Renderer가 Window보다 먼저 정리됩니다. 소멸자 본문은 멤버 해제보다 먼저 실행되므로, 여기서 `Scene::EndPlay`와 콜백 해제를 먼저 처리합니다.

**게임 세계의 데이터는 전부 `Scene` 안에 있습니다.**

```
Scene
 └─ Object (id, name, parent, transform, enabled)
     └─ Component들
         ├─ MoveComponent    (Sandbox)       ← 동작: FixedUpdate에서 위치 변경
         ├─ MeshComponent    (Graphics)      ← 데이터: 렌더러가 읽음
         ├─ ModelComponent   (Graphics)      ← 데이터: 렌더러가 읽음
         ├─ Collider         (Collision)     ← 데이터: CollisionWorld가 읽음
         ├─ Parameters       (Data)          ← 데이터: 에디터가 수치 편집
         └─ Light/Environment (Graphics)    ← 데이터: 렌더러가 읽음
```

컴포넌트는 **두 종류**로 나뉩니다.

- **동작 컴포넌트**(`MoveComponent`): `Update`/`FixedUpdate`를 직접 구현합니다. Scene이 호출해 줍니다.
- **데이터 컴포넌트**(`Collider`, `MeshComponent` 등): 스스로는 아무것도 하지 않습니다. **외부 시스템이 Scene을 훑으면서 읽어 갑니다.**
  - `CollisionWorld::Collect`는 모든 객체에서 `Get<Collider>()`를 찾습니다.
  - `CollectRenderFrame`은 모든 객체에서 `Get<MeshComponent>()`를 찾습니다.

그래서 Scene은 충돌이나 렌더링을 **모릅니다**. 충돌과 렌더링이 Scene을 읽어 갈 뿐입니다.

---

## 2. 한 프레임의 흐름 (`Application::Run` 루프)

```
┌──────────────────────────────────────────────────────────────────┐
│ ① 입력 수집                                                       │
│   Input.BeginFrame()            프레임용 기록 지우기              │
│   Window.ProcessMessages()      OS 메시지 → Input에 기록          │
│                                 (Editor.Message로 ImGui에도 전달) │
├──────────────────────────────────────────────────────────────────┤
│ ② 에디터 / 상태 판정                                              │
│   Editor.Begin(scene, camera, player, cameraData, time, input,    │
│                collision)       ← 거의 모든 것을 참조로 받음      │
│     - UI 그리기, Play/Stop, 저장/로드, Input.Capture              │
│     - 장면이 교체되면 true → Events/Collision Reset              │
│   ESC → time.paused 토글                                          │
│   simulating = (Play 중) && !정지 && !연출 차단                  │
├──────────────────────────────────────────────────────────────────┤
│ ③ 시간 진행                                                       │
│   Time.AdvanceFrame(delta)      real / presentation 시계          │
│   Editor.AdvancePresentation()  연출(Transition) 진행             │
├──────────────────────────────────────────────────────────────────┤
│ ④ 고정 tick × N회 (50Hz, 0~8회)        ← 게임 규칙은 여기서만     │
│   tickInput = Input.ConsumeTick()     입력 사본                   │
│   Time.AdvanceTick()                  combat / escape 시계        │
│   Scene.FixedUpdate({tickInput, time})  컴포넌트 동작             │
│   contacts = Collision.Step(scene, 구간)  충돌 계산               │
│   ResolveContacts(contacts)             충돌 결과 → 게임 규칙     │
│   Events.Dispatch(scene)                알림 전달                 │
│   Scene.Flush()                         삭제 예약된 것 실제 삭제  │
├──────────────────────────────────────────────────────────────────┤
│ ⑤ 프레임 갱신 (Play 중이면 1회)                                   │
│   Scene.Update({input, time.FrameView()})  시각 연출용            │
├──────────────────────────────────────────────────────────────────┤
│ ⑥ 렌더링                                                          │
│   Editor.DrawDebug()            충돌 박스 같은 디버그 표시        │
│   frame = CollectRenderFrame(scene, camera, …)  Scene → 그릴 목록 │
│   Editor.ApplyView(frame)       에디터 카메라로 덮어쓰기          │
│   Renderer.RenderScene(frame)   GPU로 그림                        │
│     └ 마지막에 overlay 콜백 → Editor.Render() (ImGui 출력)        │
└──────────────────────────────────────────────────────────────────┘
```

**④와 ⑤를 나눈 이유:** 게임 결과(이동, 충돌, 전투)는 **항상 같은 간격(0.02초)**으로 계산해야 재현됩니다. 반면 화면은 모니터 속도대로 그려야 부드럽습니다. 그래서 계산은 tick, 보여 주기는 frame이 맡습니다.

- 한 프레임에 tick을 몇 번 돌릴지는 `FixedStep::Accumulate`가 정합니다. 남은 시간은 다음 프레임으로 넘기고, 한 프레임에 최대 8회까지만 돌립니다(멈춤 현상 방지).
- `Scene::Destroy`는 삭제 표시만 합니다. 실제 메모리 해제는 tick 끝의 `Scene::Flush`에서 합니다.
- `Scene::Update`는 `FrameView()`로 combat·escape delta를 0으로 만든 시간을 받습니다. 게임 규칙이 프레임 갱신에서 실행되지 않게 하기 위해서입니다.

---

## 3. 모듈 사이에 무엇이 넘어가나

| 보내는 쪽 → 받는 쪽 | 넘어가는 것 | 형태 |
|---|---|---|
| Win32Window → Input | 키 누름·뗌, 마우스 이동량, 휠, 포커스 | 함수 호출 (`SetKey` 등). 창이 `Input*`를 들고 있음 |
| Win32Window → Editor | 원본 Windows 메시지 | 콜백 (`SetMessageHandler`) |
| Application → Scene | **`FrameContext{input, time}`** | `const` 참조 묶음. 컴포넌트는 읽기만 가능 |
| Scene → Component | `Object&`, `Scene&`, `FrameContext` | 가상 함수 인자 |
| Scene → CollisionWorld | Scene 전체 (`const Scene&`) | 충돌이 `Collider`와 `World()` 행렬을 읽음 |
| CollisionWorld → Application | **`std::vector<Contact>`** | 값 (Enter / Stay / Exit, 상대 id, 시각) |
| Application → Events | `Event{name, 객체, epoch, …}` | `Publish`로 대기열에 넣고, `Dispatch`에서 구독자 호출 |
| Scene → Renderer | **`RenderFrame`** | 값. Scene을 직접 넘기지 않고 "그릴 목록"으로 변환해서 넘김 |
| Editor → Renderer | ImGui 그리기 | 콜백 (`SetOverlay`) |
| Editor → Time | `paused`, `Reset()` | 참조로 직접 수정 |
| Editor → Input | `Capture`, `DiscardHeld` | 참조로 직접 수정 |
| Editor(Transition) → Application | `BlocksCombat()` | bool. 연출 중이면 tick 중단 |

기억해 둘 경계는 두 가지입니다.

- **렌더러는 Scene을 모릅니다.** `RenderFrame`이라는 중간 데이터만 받습니다. 그래서 Scene 구조가 바뀌어도 렌더러가 받는 영향이 적습니다.
- **컴포넌트는 Input/Time을 바꿀 수 없습니다.** `FrameContext`가 `const&`이기 때문입니다.

---

## 4. Edit 모드와 Play 모드

에디터가 켜져 있으면 프로그램은 **Edit 모드로 시작**합니다. 이때 `Scene::BeginPlay`를 부르지 않습니다.

```
[Edit 모드]  scene.m_playing = false
   - Scene.Update/FixedUpdate는 바로 return (컴포넌트 실행 안 함)
   - 에디터로 객체 배치, 수치 편집, 저장/로드만 가능
        │
        │ Play 버튼
        ▼
   m_playSnapshot = 현재 편집 상태를 문자열로 저장 (SceneIO::Encode)
   time.Reset(), input.DiscardHeld()
   scene.BeginPlay()  → 모든 컴포넌트 Awake → OnEnable → Start
        │
[Play 모드]  scene.m_playing = true
   - tick/frame 루프에서 컴포넌트 실행
        │
        │ Stop 버튼
        ▼
   SceneIO::Decode(m_playSnapshot) → 장면 통째로 교체
   (Play 중 바뀐 위치 등은 모두 버려지고 Play 직전 상태로 돌아감)
```

에디터 없이 실행하면 시작할 때 `BeginPlay`를 바로 부르고, 계속 Play 상태로 돌아갑니다.

---

## 5. 나머지 모듈이 들어가는 자리

| 모듈 | 역할 | 누가 쓰나 |
|---|---|---|
| **Reflection** (`ComponentRegistry`) | "문자열 이름 ↔ 컴포넌트 타입" 표. 이름으로 컴포넌트를 만들고 필드를 읽고 씀 | SceneIO(저장/로드), Editor(인스펙터, 컴포넌트 추가 메뉴) |
| **Data** (`SceneIO`) | Scene ↔ 텍스트(JSON) 변환. `Encode`/`Decode`, `Save`/`Load` | Editor (파일 저장, Play 스냅샷, Undo 기록) |
| **Resource** (`Model`, `ModelCache`) | 모델 파일 로드와 공유 캐시 | Editor가 소유. SceneIO가 로드할 때 받아서 씀 |
| **Presentation** (`Transition`) | 페이드·연출 진행, `BlocksCombat` | Editor가 소유. Application은 결과만 확인 |
| **Animation** (`Timeline`) | 시간에 맞춰 이벤트를 꺼내는 타임라인 | **런타임에서는 아직 미사용** (테스트만 사용) |
| **Graphics** (`RenderFrame`, `D3D11Renderer`) | Scene → 그릴 목록 변환, GPU 출력 | Application |
| **Tests** | `--engine-tests` 등의 실행 모드 | Main.cpp가 옵션에 따라 호출 |

### 의존 방향 (위가 아래를 사용)

```
            Main
              │
         Application
   ┌────┬─────┼──────┬──────────┬────────┐
 Editor │   Window  Renderer  Collision  Events
   │    │     │        │          │
   ├─ SceneIO ─ Reflection        │
   ├─ Resource                    │
   ├─ Presentation                │
   │    │                         │
   └────┴──────► Scene ◄──────────┘   (모두 Scene을 읽거나 씀)
                   │
              Input, Time            (Scene은 FrameContext로만 앎)
```

**Scene은 아래쪽 기반**이고, 위쪽 모듈을 모릅니다. Scene이 아는 것은 `Input`과 `Time`(FrameContext 정의용)뿐입니다.

---

## 6. 아직 비어 있는 연결 (기준 커밋 시점)

자리는 있지만 아직 실제 동작이 연결되지 않은 부분입니다.

- **`ResolveContacts`**: 충돌 결과를 받는 자리입니다. 지금은 트리거 Enter 로그만 남깁니다.
- **`Events`**: 런타임 코드에서 `Publish`나 `Subscribe`를 부르는 곳이 없습니다. `Dispatch`는 매 tick 실행되지만 대기열이 늘 비어 있습니다.
- **`Timeline`**: 테스트에서만 쓰입니다.
- **카메라**: `m_camera` 객체에는 이동 컴포넌트가 없습니다. 위치가 고정되어 있고, Play 중이 아니면 에디터 카메라가 `ApplyView`로 덮어씁니다.
