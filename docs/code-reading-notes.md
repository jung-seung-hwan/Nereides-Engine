# Nereides Engine 코드 학습 기록과 다음 분석 지점

작성일: 2026-10-09 (Asia/Seoul)

## 1. 문서 목적과 현재 진행 상태

사용자가 엔진 구조를 직접 이해하고 팀원에게 설명할 수 있도록, 대화에서 설명한 코드와 주석 검토 결과를 정리한 학습 기록이다. 현재 구현과 향후 제안을 구분한다. 이 문서는 새로운 구현 명세나 구현 완료 보고서가 아니다.

- 설명한 범위: `Main.cpp`, `Application.cpp`, `Scene.cpp`, `Scene.h`.
- 다음 상세 분석 대상: `MoveComponent.h`.
- 학습 방식: 실제 코드를 따라가며 역할, 호출 관계, C++ 문법, 동작 예시를 설명한다.
- 사용자는 코드에 직접 이해한 내용을 주석으로 기록한다. 주석 검토 요청 시에는 평가만 하고 임의로 변경하지 않는다.
- 이 문서를 추가하는 작업에서는 기존 코드와 주석을 수정하지 않았다.
- 아래 내용은 작성일에 다시 읽은 코드 기준이다. 이후 세션에서는 파일의 최신 상태를 확인한다.

### 관련 파일

| 파일 | 역할 | 진행 상태 |
|---|---|---|
| [Main.cpp](<C:/Users/User/Desktop/Nereides Engine/src/Main.cpp>) | Windows 진입점, 경로 설정, 실행 모드 선택 | 설명 완료 |
| [Application.h](<C:/Users/User/Desktop/Nereides Engine/src/Core/Application.h>) | Application이 소유하는 시스템, 실행 옵션 | 관련 구조 설명 |
| [Application.cpp](<C:/Users/User/Desktop/Nereides Engine/src/Core/Application.cpp>) | 초기화와 메인 루프, 시스템 실행 순서 | 설명 완료 |
| [Scene.h](<C:/Users/User/Desktop/Nereides Engine/src/Scene/Scene.h>) | 객체·컴포넌트·장면의 타입과 인터페이스 | 설명 완료 |
| [Scene.cpp](<C:/Users/User/Desktop/Nereides Engine/src/Scene/Scene.cpp>) | 객체 관리, 단계별 업데이트, 변환과 카메라 행렬 | 설명 완료 |
| [MoveComponent.h](<C:/Users/User/Desktop/Nereides Engine/src/Sandbox/MoveComponent.h>) | 입력과 시간을 사용한 이동 예제 | 다음 설명 대상 |

## 2. 전체 구조: 무엇이 무엇을 소유하는가

```text
wWinMain
 └─ Application
     ├─ Input / Time
     ├─ Win32Window / D3D11Renderer
     ├─ Editor
     ├─ CollisionWorld
     ├─ 카메라 Object ID / Camera 설정
     └─ Scene
         └─ Object 목록
             ├─ ID / 이름 / 부모 ID
             ├─ Transform
             ├─ 활성 여부
             └─ Component 목록
```

객체에 기능 단위를 붙이는 것이 현재 게임 객체 구성의 기본이다. 하지만 엔진 전체의 최상위가 Object인 것은 아니다. Application이 여러 시스템과 Scene을 소유하고, Scene이 게임 객체들을 소유한다.

소유한다는 것은 대상의 수명과 메모리 해제를 책임진다는 의미다.

```cpp
// Scene이 Object를 소유
std::vector<std::unique_ptr<Object>> m_objects;

// Object가 Component를 소유
std::vector<std::unique_ptr<Component>> m_components;
```

부모·자식 관계는 메모리 소유 관계와 다르다. 부모와 자식은 모두 Scene이 소유한다. 자식은 부모의 ID만 저장하며, 부모 삭제 시 자손까지 삭제되는 것은 Scene의 삭제 로직이 그렇게 처리하기 때문이다.

## 3. Main.cpp: 프로그램 시작과 실행 모드 선택

### wWinMain

Windows GUI 프로그램의 진입점이다.

```cpp
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE,
                    PWSTR commandLine, int showCommand)
```

- `instance`: 현재 프로그램 인스턴스 핸들. 창 생성 등에 전달한다.
- 이름 없는 두 번째 인수: 현재 코드에서는 사용하지 않는다.
- `commandLine`: 실행 옵션 문자열.
- `showCommand`: 창 표시 방법.
- `WINAPI`: Windows API에서 사용하는 호출 규약 표현.
- 반환하는 정수: 프로그램 종료 코드.

### 상대 경로 기준 설정

`GetModuleFileNameW`로 실행 파일 경로를 얻고, 실행 파일 폴더부터 상위 폴더를 탐색한다. `NereidesEngine.sln` 또는 `data/presentation/preview.json`이 있는 곳을 찾으면 현재 작업 디렉터리로 설정한다.

Visual Studio, 탐색기, 배포 폴더 등 실행 위치가 달라도 데이터의 상대 경로를 일관되게 해석하기 위한 처리다. 조건에 맞는 폴더를 찾지 못하면 작업 디렉터리를 바꾸지 않는다.

### 실행 옵션 분기

`std::wstring_view`는 명령행 문자열을 소유하거나 복사하지 않고 바라보는 뷰다.

- `--engine-tests`, `--editor-workflow-test`, `--model-test 경로`: 별도 테스트 함수로 직접 이동한다.
- 인수 없음: 기본 옵션으로 에디터를 실행한다.
- 그 밖의 지원되는 검사 옵션: 먼저 `automatic = true`, `editor = false`를 설정하고 필요한 플래그를 추가한다.
- `--smoke-test`: 기존 삼각형 출력 검사.
- `--scene-smoke-test`: 장면 출력 검사. 분기 본문이 비어 있는 것은 앞에서 설정한 값으로 충분하기 때문이다.
- `--editor-smoke-test`: 에디터를 포함한 자동 검사.
- `--presentation-smoke-test`: 에디터와 연출 미리보기 검사.
- `--resize-smoke-test`, `--editor-resize-smoke-test`: 크기 변경 검사.
- `--benchmark`: 성능 측정.
- 알 수 없는 옵션: 오류 로그 후 종료 코드 2.

`resize`나 `benchmark` 플래그 자체가 자동 종료를 의미하지는 않는다. 현재 명령행 분기에서 함께 설정되는 `automatic`과 조합되어 동작한다. 현재 파서는 일반적인 복수 옵션 조합 파서가 아니라 정해진 문자열을 비교하는 방식이다.

### Application 호출과 오류 처리

```cpp
nereides::Application application;
return application.Run(instance, showCommand, options);
```

실질적인 시스템 초기화와 실행은 Application에 맡긴다. `Run()`에서 전달된 `std::exception`은 오류 로그를 남기고 종료 코드 1로 처리한다. 이 `try`는 함수 전체가 아니라 `Run()` 호출을 감싸므로, 앞의 경로 처리와 직접 실행하는 테스트까지 모두 보호하는 것은 아니다.

## 4. Application.cpp: 시스템을 연결하는 실행 흐름

Application은 개별 캐릭터 행동을 구현하는 곳이 아니라 입력, 시간, 장면, 충돌, 렌더러, 에디터를 연결하고 실행 순서를 관리하는 곳이다.

### 초기화

1. 실행 옵션으로 자동 검사와 에디터 사용 여부를 결정한다.
2. 창에 입력 시스템을 연결하고 창을 만든다.
3. 렌더러를 초기화한다.
4. 플레이어, 충돌 대상, 바닥, 카메라 등 기본 예제 장면을 만든다.
5. 에디터 사용 시 에디터를 초기화하고 창 메시지·오버레이 콜백을 연결한다.

예제 객체 생성은 에디터 여부를 검사하기 전에 실행된다. 따라서 주석의 ‘에디터 예제’는 에디터 전용이라는 뜻으로 읽으면 안 된다.

```cpp
auto& player = m_scene.Create("Example player");
player.Add<MoveComponent>();
```

Scene이 객체를 만들고, 그 객체가 이동 컴포넌트를 소유하게 되는 과정이다.

### 메인 루프

```text
입력 프레임 준비 / Windows 메시지 처리
 → 실제 경과 시간 측정
 → 최소화 상태면 대기 후 이번 반복 생략
 → 에디터 프레임 처리
 → 일시정지·포커스·연출 상태 반영
 → 시간 갱신
 → 연출 진행
 → 실행 조건이 맞으면 Scene::Update
 → 전투 시간이 진행했으면 충돌 검사
 → 디버그 표시와 렌더러 크기 갱신
 → 렌더링 데이터 수집과 렌더 호출
 → 자동 검사·성능 기록·종료 조건 처리
```

주의할 조건:

- 일반 실행의 `delta`는 실제 측정한 가변 시간이다. 연출 테스트는 `1/60`을 사용한다.
- 최소화 대기 후에는 시간 기준을 재설정해 대기 시간을 다음 이동 계산에 포함하지 않는다.
- `Editor::Begin()` 뒤의 충돌·입력 초기화는 매 프레임 무조건 수행하는 것이 아니라, 반환값이 장면 교체를 알릴 때 수행한다.
- Scene 업데이트는 에디터가 없거나 에디터가 Play 상태일 때 실행한다.
- 일시정지 시 모든 함수 호출이 멈추는 것은 아니다. 시간의 delta가 0이 되는 것과 Update 호출 자체를 생략하는 것은 다르다.
- 현재 충돌 검사는 Scene 업데이트 뒤에 실행된다. 새 충돌 결과가 이미 끝난 컴포넌트 Update에 자동으로 전달되는 구조는 아니다.
- 렌더링 데이터 수집은 Scene을 읽어 그릴 데이터를 구성하는 단계이며, 실제 출력은 렌더러가 담당한다.
- 벤치마크는 Present를 포함한 루프의 경과 시간을 기록한다. 순수 GPU 실행 시간을 측정한 결과가 아니다.

## 5. 시간, 고정 tick, 렌더 프레임

### delta time과 고정 tick

`delta time`은 두 갱신 사이에 경과한 시간이고, 고정 tick은 게임 상태를 일정한 시간 간격으로 계산하는 실행 규칙이다. 서로 대체하는 별도 카운터라고만 생각하면 부족하다. 고정 tick에서도 한 번의 계산에 사용할 delta가 있으며, 60Hz라면 보통 `1/60초`다.

예를 들어 속도가 초당 3이라면 이동량은 `3 × delta`다.

```text
가변 업데이트: 프레임마다 실제 경과 시간으로 한 번 계산
고정 업데이트: 누적된 시간에 따라 1/60초 계산을 0번 이상 수행
렌더링: 화면을 출력할 프레임마다 수행
```

고정 tick은 계산 간격을 일정하게 하여 전투·물리·시간 판정의 동작을 예측하기 쉽게 한다. 큰 delta로 한 번에 이동하는 상황을 줄이는 데 도움이 되지만, 모든 충돌 누락을 자동으로 해결하지는 않는다. 빠른 물체는 한 tick 안에서도 얇은 대상을 통과할 수 있으므로 연속 충돌 검사 등 별도 대책이 필요할 수 있다. 결정성도 고정 tick 하나만으로 보장되지 않는다.

현재 일반 실행에는 시간 누적 기반의 고정 60Hz 루프가 없다. 테스트에서 delta를 `1/60`로 지정하는 것만으로 그 구조가 구현되었다고 볼 수 없다.

### 렌더 호출과 게임 상태 계산

‘매 프레임 렌더링’ 자체보다 중요한 것은 게임 상태 계산 횟수와 화면 출력 횟수가 반드시 같을 필요는 없다는 점이다. 고정 tick 구조에서는 렌더 한 번 사이에 게임 계산이 0번, 1번, 여러 번 실행될 수 있다. 두 갱신 사이를 부드럽게 보이게 하는 보간은 별도 구현 항목이다.

### 여러 종류의 시간과 일시정지

전투·탈출·연출 시간을 구분하는 목적은 어떤 상황에서 어떤 시간이 진행하거나 멈추는지 시스템 공통 규칙으로 관리하기 위해서다. 전투를 멈췄다고 UI나 연출까지 모두 멈춰야 하는 것은 아니다.

각 시간의 정확한 진행·정지 조건은 앞으로 Time 코드를 읽으며 확인한다. 시간 종류의 이름만으로 현재 구현의 정책을 추측하지 않는다.

## 6. Scene.h: 타입과 사용 인터페이스

### 기본 문법

| 표현 | 의미 |
|---|---|
| `#pragma once` | 한 번의 소스 파일 컴파일에서 헤더 중복 포함 방지 |
| `namespace nereides` | 엔진의 이름 공간 |
| `class Object; class Scene;` | 뒤에 정의되는 클래스의 존재를 미리 알리는 전방 선언 |
| 함수 뒤 `const` | 해당 함수가 자신의 일반 멤버를 변경하지 않는다는 제약 |
| `noexcept` | 예외가 밖으로 나오지 않는다는 약속. 오류가 불가능하다는 뜻은 아님 |
| `final` | 해당 클래스를 상속할 수 없음 |
| `friend class` | 지정한 클래스에 private 멤버 접근 허용 |

헤더에는 함수 선언뿐 아니라 짧은 함수와 템플릿의 구현도 들어 있다. `.h`가 항상 선언만, `.cpp`가 항상 구현만 담는다고 단정하지 않는다.

### ObjectId

`using ObjectId = std::uint64_t;`는 객체 식별 번호에 사용할 64비트 부호 없는 정수의 별칭이다. 객체 자체나 포인터가 아니다. ID로 `Scene::Find()`를 호출해 객체를 찾는다.

ID 생성 카운터는 현재 프로세스 내 Scene 생성·Clear 등에서 재사용되지 않도록 구성되어 있다. 실행 파일을 종료하고 다시 실행한 뒤에도 전역적으로 유일한 영구 ID라는 뜻은 아니다.

### FrameContext

```cpp
struct FrameContext
{
    const Input& input;
    const Time& time;
};
```

입력과 시간 원본을 복사하지 않고 참조한다. `const`는 이 참조를 통해 수정하지 못하게 한다. 원본이 프로그램 전체에서 절대 변하지 않는다는 뜻은 아니다.

```text
Application → FrameContext → Scene::Update → Component::Update
```

현재 FrameContext에는 충돌 결과나 리소스 접근 기능이 포함되어 있지 않다. 프레임 컨텍스트를 컴포넌트가 장기간 보관하는 설계도 현재 설명 범위에 없다.

### Transform

- 위치 기본값: `(0, 0, 0)`.
- 회전 기본값: `(0, 0, 0)`, 현재 행렬 계산은 라디안을 사용한다.
- 크기 기본값: `(1, 1, 1)`.
- `Matrix()`: 로컬 크기·회전·이동을 행렬로 조합한다.

현재 Transform은 Component 파생형이 아니라 모든 Object가 기본으로 가진 멤버다.

### Component와 UpdatePhase

```cpp
virtual ~Component() = default;
virtual void Update(Object&, Scene&, const FrameContext&) {}
virtual UpdatePhase Phase() const { return UpdatePhase::Logic; }
```

- 가상 소멸자: Component 포인터로 삭제해도 파생 컴포넌트의 정리가 실행되도록 한다.
- `virtual Update`: 각 컴포넌트가 자신의 행동을 구현할 수 있는 공통 함수.
- 기본 Update는 비어 있으므로 데이터만 가진 컴포넌트도 가능하다.
- `Phase()`를 재정의하지 않으면 Logic 단계다.
- `enabled = false`: 컴포넌트를 보관하되 업데이트 대상에서 제외.
- `m_removed = true`: 삭제 예약. 실제 제거는 Flush에서 진행.

Update 인수는 순서대로 자신을 가진 Object, Scene 접근, 입력·시간 정보다. Component는 현재 자신의 소유 객체 포인터를 멤버로 저장하기보다 Update 인수로 전달받는다.

`enum class UpdatePhase`는 단계 값을 정의한다. 실제 실행 순서를 만드는 것은 Scene.cpp의 반복문이다.

### Object와 컴포넌트 추가·조회·제거

Object는 `final`이다. 현재 구조는 Object 상속으로 플레이어·적을 구분하기보다 필요한 컴포넌트를 조합하는 방식이다.

| 기능 | 현재 동작 |
|---|---|
| 생성자 | ID와 이름 초기화 |
| `Id`, `Parent`, `Name` | 식별 번호, 부모 ID, 이름 조회 |
| `SetName` | 이름 변경 |
| `Add<T>(args...)` | T 컴포넌트 생성, 소유 목록에 추가, 참조 반환 |
| `Get<T>()` | 삭제 예약되지 않은 컴포넌트 중 T로 변환 가능한 첫 항목 반환 |
| `Remove<T>()` | 첫 일치 항목 비활성화 및 삭제 예약 |
| `Components()` | 삭제 예약되지 않은 컴포넌트의 읽기 전용 포인터 목록 반환 |

`Add<T>()` 내부 흐름:

```cpp
auto value = std::make_unique<T>(std::forward<Args>(args)...);
auto& result = *value;
m_components.push_back(std::move(value));
return result;
```

1. 컴포넌트를 생성하고 unique_ptr로 소유한다.
2. 실제 컴포넌트의 참조를 확보한다.
3. Object의 목록으로 unique_ptr 소유권을 옮긴다.
4. 컴포넌트를 설정하거나 사용할 참조를 반환한다.

`Args...`는 여러 생성자 인수를 받을 수 있게 하고, `std::forward`는 전달 특성을 유지한다. `std::move` 자체가 이동 작업을 실행하는 것은 아니며, 이후 연산이 이동을 선택할 수 있도록 표현식을 변환한다.

`Get<T>()`의 `dynamic_cast`는 T로 다룰 수 있는 컴포넌트인지 검사한다. 찾지 못하면 nullptr다. 비활성 컴포넌트도 조회할 수 있고, 같은 타입이 여러 개면 첫 항목만 반환한다. 현재 Add에는 같은 타입의 중복 부착을 막는 규칙이 없다.

참조와 포인터는 소유권을 주지 않는다. 실제 삭제 이후에는 이전에 받은 참조나 포인터를 사용하면 안 된다. `Components()`는 컴포넌트 자체가 아니라 포인터 목록을 복사한다.

### Scene과 Camera

Scene은 객체 목록과 업데이트 중 여부를 보관한다. Create, Find, Destroy, Update, SetParent 등의 실제 구현은 Scene.cpp에 있다.

Camera는 현재 Component를 상속하지 않는 별도 구조체다.

```text
카메라 역할 Object → 위치·회전·크기
Camera 데이터     → 시야각·near·far와 행렬 계산 함수
```

`verticalFov`의 기본값은 π/4, 즉 45도다. `nearPlane = 0.1`, `farPlane = 500`은 가까운/먼 클리핑 경계다. `aspect`는 화면 가로/세로 비율이다.

## 7. Scene.cpp: 관리와 계산의 실제 동작

### 생성과 조회

- Create는 고유 ID를 할당하고 객체를 Scene의 소유 목록에 추가한다.
- Find는 목록을 선형 탐색하며 삭제 예약된 객체는 반환하지 않는다.
- Objects는 삭제 예약되지 않은 객체들의 ID를 반환한다. 비활성 객체는 포함한다.
- Active는 자신과 부모, 그 위 조상까지 확인한다. 자신만 enabled여도 부모가 비활성이면 업데이트되지 않는다.

### 삭제 예약과 Flush

Destroy는 대상 및 자손을 삭제 예약한다. 예약 즉시 Find와 Objects에서 보이지 않지만, 메모리가 즉시 해제되는 것은 아니다.

Flush는 업데이트 중이면 아무 작업 없이 반환한다. 업데이트 중이 아니면 삭제 예약된 컴포넌트와 객체를 실제로 제거한다.

Clear는 전체 객체를 삭제 예약한 다음 Flush를 호출한다. 따라서 업데이트 밖에서 Clear를 호출하면 그 호출 안에서 실제 삭제까지 이루어진다.

현재 Scene::Update는 내부 마지막에 Flush를 호출한 뒤 반환한다. Application의 충돌 검사보다 먼저 실행되므로 ‘프레임 전체 종료’ 또는 ‘전투 tick 전체 종료’와 동일한 시점으로 보면 안 된다.

### 단계별 Update

```text
같은 Scene의 중복 진입 검사
 → 대상 Object ID와 Component 포인터 목록 확보
 → 전체 객체의 Logic 컴포넌트 실행
 → 전체 객체의 Animation 컴포넌트 실행
 → 전체 객체의 Late 컴포넌트 실행
 → 업데이트 중 표시 해제
 → Flush
```

객체 A의 모든 단계를 마친 다음 B를 처리하는 구조가 아니다.

```text
A Logic → B Logic → A Animation → B Animation → A Late → B Late
```

처음 확보하는 목록은 객체와 컴포넌트의 깊은 복사가 아니라 ID와 포인터 목록이다. 목록 확보 후 실행 중 추가된 객체·컴포넌트는 다음 Update 호출부터 업데이트 대상이 된다. 그 객체가 현재 프레임의 뒤쪽 충돌·렌더링에도 반드시 제외된다는 의미는 아니다.

예외가 발생하면 남은 업데이트를 중단한다. `m_updating`을 해제하고 Flush로 정리한 다음 다시 throw한다. 이미 수행한 변경을 되돌리지도, 나머지 객체 업데이트를 이어서 수행하지도 않는다.

### 로컬 변환, 부모 연결, 월드 변환

현재 행렬 구성은 다음과 같다.

```text
Local = Scale × Rotation × Translation
World = Local × ParentLocal × GrandparentLocal × ...
```

현재 행 벡터 방식에서 정점에는 크기 → 회전 → 이동 순서로 적용된다. ‘행 우선 저장’은 메모리 배치, ‘행 벡터’는 수학적 곱셈 방식이므로 구분한다.

SetParent는 부모 존재 여부와 순환 관계를 검사한 후 부모 ID만 변경한다. 로컬 위치·회전·크기는 재계산하지 않는다.

| 조건 | 자식 로컬 X | 부모 월드 X | 자식 월드 X |
|---|---:|---:|---:|
| 부모 없음 | 2 | 해당 없음 | 2 |
| 현재 방식으로 부모 연결 | 2 | 10 | 12 |
| 월드 위치 유지 기능을 구현한다면 | -8 | 10 | 2 |

위 예시는 회전과 크기 변화가 없는 경우다. 일반적인 월드 변환 유지 계산은 현재 행렬 규약에서 `새 Local = 이전 World × 새 부모 World의 역행렬`이다. 역행렬이 없거나 결과를 현재 위치·회전·크기만으로 표현할 수 없는 경우의 정책도 필요하다.

### 카메라 View와 Projection

- View: 카메라 객체의 월드 행렬의 역행렬. 월드 좌표를 카메라 기준 좌표로 바꾼다.
- Projection: 시야각·화면 비율·near·far를 사용해 원근 투영을 계산한다.
- 개념적인 변환 순서: 로컬 좌표 → World → View → Projection → 원근 나눗셈과 화면 좌표 변환.

Projection 함수가 소스에서 View보다 위에 있어도 실행 순서나 변환 순서와 관계없다. 함수 정의의 배치 순서와 실제 호출·행렬 곱셈 순서는 구분한다.

## 8. 사용자 주석 검토 기록

### 핵심 세 항목과 최신 상태

| 이전 주석 | 검토 내용 | 2026-10-09 확인 상태 |
|---|---|---|
| 위치 × 회전 × 크기 | 실제 코드는 크기 × 회전 × 이동 | 사용자가 올바른 순서로 수정한 상태. ‘행 우선’과 ‘행 벡터’의 차이는 계속 구분할 것 |
| 오류 발생해도 나머지 처리 진행 | 남은 업데이트는 중단하고 정리 후 재전달 | 중단·정리·오류 전달을 명시하도록 수정된 상태 |
| 부모 설정 시 상대적으로 변환됨 | 새 로컬 좌표를 계산하는 것이 아니라 기존 값을 새 부모 기준으로 해석 | 그 의미로 수정된 상태 |

다음 세션에서는 이 세 항목을 아직 수정되지 않은 오류로 다시 지적하지 않는다. 최신 파일을 먼저 확인한다.

### 나머지 보충 사항

| 주석의 주제 | 기억할 조건 |
|---|---|
| 실행 모드 표 | 자동 종료는 automatic 등 옵션 조합에 의해 결정 |
| 에디터 예제 등록 | 기본 예제 장면은 에디터 없이 실행할 때도 생성 |
| 에디터 프레임 처리 | 뒤의 입력·충돌 Reset은 Begin 반환 조건이 참일 때 |
| 모든 객체 ID 반환 | 삭제 예약된 객체 제외, 비활성 객체 포함 |
| Clear의 삭제 예약 | 업데이트 밖에서는 같은 호출에서 실제 삭제 가능 |
| Update 후 Flush | Update 내부 끝에서 호출하며 Clear에서도 호출 가능 |
| 활성 여부 확인 | 자기 자신뿐 아니라 조상까지 확인 |
| 새 객체는 다음 프레임부터 | 업데이트 목록 확보 후 추가된 대상의 Update에 관한 규칙 |

이 항목들은 대체로 이해가 틀렸다기보다 설명에 조건이 생략된 경우다.

## 9. 사용자가 적은 향후 구현 항목과 검토 의견

아래는 검토 의견이며 구현 완료 사항이나 확정된 작업 지시가 아니다.

| 항목 | 권장 방향 | 시점 판단 |
|---|---|---|
| 부모 변경 시 변환 유지 옵션 | KeepLocal / KeepWorld 구분. 위치뿐 아니라 회전·크기도 고려하고 Undo/Redo 연동 | 에디터 사용성을 위해 우선 고려 |
| 태그 기반 검색 | ID 조회와 별도 제공. 한 태그로 여러 객체를 찾을 수 있는 계약, 저장·로드·Undo/Redo까지 정의 | 실제 게임 코드 요구에 맞춰 추가 |
| 업데이트 우선순위 | 현재 Phase를 먼저 활용하고 같은 Phase 안의 컴포넌트 순서가 필요한지 검토 | 구체적인 실행 의존성이 생길 때 |
| 선형 Find 개선 | 필요하면 ID→객체 검색 인덱스 추가. 생성·삭제·로드와 인덱스 일관성 유지 | 객체 규모와 비용을 측정한 뒤 판단 |

태그는 분류이고 ID는 특정 객체의 식별자다. 태그 검색이 ID 검색을 대체하지 않는다. 충돌 layer/mask와 태그도 목적이 다르다.

에디터 Hierarchy의 표시 순서와 게임 실행 순서를 동일하게 취급하지 않는다. 우선순위를 도입하더라도 Logic→Animation→Late의 단계 계약을 유지하고 같은 우선순위일 때의 순서도 정의해야 한다.

KeepWorld는 부모의 크기가 0이거나 비균등 크기와 회전이 결합되어 전단이 생기는 경우까지 고려해야 한다. 모든 행렬이 현재 TRS 표현으로 정확하게 분해되는 것은 아니다.

### 엔진 목표와 관련해 따로 남은 기반 작업

- 일반 실행의 고정 60Hz tick: 시간 누적, 따라잡기 정책, tick별 입력 소비 등을 함께 설계해야 한다.
- 삭제의 최종 시점: Scene 업데이트, 충돌, 이벤트 처리 중 어디까지를 한 tick으로 볼지 정하고 실제 삭제 경계를 맞춰야 한다.

이 두 항목은 이번 문서 작업에서 구현하지 않았다.

## 10. 다음 세션에서 이어갈 분석

### 바로 다음 파일: MoveComponent.h

Scene.h 설명은 완료했다. 다음에는 실제 게임 행동 예제로 넘어가 아래 연결을 코드로 확인한다.

```text
Application에서 Object 생성
 → Add<MoveComponent>()로 부착
 → Scene::Update가 컴포넌트 Update 호출
 → FrameContext에서 입력과 시간 조회
 → Object의 로컬 위치 변경
 → 후속 충돌 검사와 렌더링에 반영
```

설명할 질문:

1. `MoveComponent final : public Component`와 `override`는 무엇을 뜻하는가?
2. Update의 `Scene&` 인수에 이름이 없는 이유는 무엇인가?
3. D−A, W−S로 이동 방향을 만드는 이유는 무엇인가?
4. `sqrt(x*x + z*z)`와 길이 나눗셈이 대각선 이동 속도에 어떤 영향을 주는가?
5. `speed × combat.delta`는 무엇을 계산하는가?
6. 일시정지로 combat.delta가 0이면 왜 이동하지 않는가?
7. 직접 변경하는 position은 로컬 좌표이므로 부모가 있으면 어떤 차이가 생기는가?
8. 이 예제에 최종 플레이어 제어, 충돌 반응, 카메라 기준 이동까지 구현되어 있는가?

현재 MoveComponent는 입력·시간·객체 연결을 보여주는 예제이며 최종 플레이어 컨트롤러가 아니다. 세부 설명은 다음 세션에서 진행한다.

그 후 권장 읽기 순서는 Input → Time → Collision → 렌더링 데이터 수집이다. 사용자의 이해와 질문에 따라 한 파일씩 진행한다.

### 다른 세션에 전달할 요청 예시

> `C:\Users\User\Desktop\Nereides Engine\docs\code-reading-notes.md`를 먼저 읽어줘. Main.cpp, Application.cpp, Scene.cpp, Scene.h까지 설명을 들었고, 다음은 src/Sandbox/MoveComponent.h부터 분석하려고 해. 실제 최신 코드를 확인하고 문법, 역할, 호출 흐름, 예시를 연결해서 설명해줘. 지금은 학습과 분석 단계이므로 코드나 내가 적은 주석은 요청 없이 수정하지 마. 현재 구현과 향후 구현 제안을 구분해줘.
