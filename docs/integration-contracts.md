# 콘텐츠와 렌더 연결 안내

2026-10-06 공통 기반의 실제 API다. 단계별 구현 이유는 [로드맵](implementation-roadmap.md)을 읽는다. 이 문서의 임시 규격은 팀 통합에서 검증하며 원문에서 미정인 게임 수치를 확정하지 않는다.

## 객체와 프레임

`Scene::Create`가 객체를 소유하고 `Object::Add<T>`가 행동을 붙인다. 사용자 코드는 ObjectId를 보관하고 `Scene::Find`로 유효성을 확인한다. 객체/컴포넌트 포인터는 소유권이 없고 삭제 Flush 이후 보관하면 안 된다. 프레임 중 생성·부착은 다음 프레임부터 갱신하며 제거는 즉시 비활성, 메모리 해제는 갱신 뒤다.

현재 전역 갱신 단계는 Logic → Animation → Late다. 콘텐츠 행동은 기본 Logic, ModelComponent는 Animation, 추적 카메라 같은 후처리는 Phase를 Late로 지정한다. 생성 순서가 애니메이션을 행동보다 먼저 실행시키지 않도록 한다. Transform 월드 행렬은 조회 시 계산한다. 같은 단계의 순서는 객체/컴포넌트 등록 순서다.

그다음 CollisionWorld::Step → 콘텐츠 결과 해석 → 이벤트 전달 → CollectRenderFrame → 렌더 순서로 연결한다. 샌드박스는 충돌을 로그로만 표시한다. 전투 결과 처리를 새 엔진 규칙으로 추가하지 말고 콘텐츠의 명시적인 처리 지점에서 수행한다.

## 입력과 시간

`FrameContext`에서 Input과 Time을 읽는다. Pressed/Held/Released는 Windows 가상 키 값 기준이다. 이벤트 공급은 Win32, 마우스 상대 이동은 Raw Input이다. UI에 입력을 넘기거나 전환·재시작할 때 DiscardHeld/Capture로 이전 입력을 제거한다. 콘텐츠의 예약 입력은 별도로 정리해야 한다.

Time의 combat은 배율 적용, escape는 배율 미적용, presentation은 별도 연출 시간이다. paused는 게임 시계를 멈추며 pausePresentation은 연출 정지를 정한다. combatStopped는 구역 진입 후 연출 구간에 사용한다. 탈출 구역에 들어가기 전의 전투를 멈추는 용도로 쓰지 않는다.

## 모델과 공격

ModelCache::Load → Object::Add<ModelComponent> → player.Play(clipIndex, loop, blendSeconds) 순으로 사용한다. 모델/클립은 immutable 공유 데이터이며 애니메이션 시간·포즈는 객체별로 가진다. 현재 구현은 모델과 클립이 함께 들어 있는 FBX를 대표 검증했다. 별도 모션 파일 리타게팅은 후속 작업이다.

공격 식별자는 콘텐츠가 공격마다 새로 부여한다. Timeline으로 시작/판정 활성/종료 마커를 받고 같은 공격 시각으로 EvaluateTrajectory를 평가한다. 긴 프레임에서 여러 마커를 통과하면 모두 처리해야 한다. 곡선의 연속 판정은 중간 시점의 샘플을 제공한다. Collider.attack에는 이 식별자를, layer/mask에는 대상을 설정한다.

CollisionWorld는 후보를 반환한다. HitLedger는 콘텐츠가 적중을 수락한 뒤에만 Record한다. 패링/무적/공격체 파괴 우선순위를 판단하기 전에 후보부터 중복 소모시키지 않는다. Contact.fraction을 사용하면 전투 시각으로 얻은 후보를 해당 프레임의 탈출 시각에 대응시킬 수 있다. 현재 position은 접촉 당시 첫 객체 중심 근사값이며 정밀한 접촉 법선은 제공하지 않는다.

## 렌더 포팅

RenderFrame은 CPU 메시 공유 참조·월드/뷰/투영·기본색·스키닝 행렬·시간을 제공한다. 모델 정점/인덱스는 변경 없는 리소스다. 지금의 D3D11Renderer는 unlit 검증 백엔드이며 Scene이나 콘텐츠 클래스를 직접 읽지 않는다. Sherlock의 Renderer/BuildDrawList 입력을 이 경계에 맞춰 바꾸고, 광원·환경·최종 재질·그림자용 입력은 렌더 담당과 추가한다.

좌표는 +Y 위·+Z 앞, 미터·라디안, row vector·local × parent다. HLSL은 row_major이고 WVP는 world × view × projection이다. 뼈 행렬은 mesh inverse bind × animated global이다. 한 메시당 최대 127개 뼈와 fallback 한 개를 현재 검증 백엔드에서 지원한다. 메인/그림자 패스가 같은 포즈를 사용하도록 포팅 시 확인한다.

장치·스왑체인·GPU 캐시는 렌더러 하나가 소유한다. UI는 그 장치/컨텍스트를 빌려 쓴다. CPU 메시의 마지막 참조가 사라지면 다음 프레임에서 해당 GPU 캐시를 정리한다. 변경은 기존 메시를 수정하지 말고 새 immutable 데이터로 교체한다.

## 저장과 재시작

에디터는 09~10단계부터 Edit/Play를 분리한다. EditHistory는 SceneIO::Encode/Decode의 메모리 스냅샷을 사용하며 Undo/Redo/Stop 때 새 Scene으로 교체한다. 교체 후 카메라·플레이어 역할 ID와 선택 참조를 다시 얻고 충돌·입력을 초기화한다. 저장 ID는 파일 안의 정규화된 번호이며 런타임 ID를 영구 식별자로 취급하지 않는다. 새 콘텐츠 컴포넌트를 연결할 때에도 직렬화 등록이 필요하다. 등록하지 않은 컴포넌트를 조용히 누락시키지 않고 오류로 보고한다.

편집 카메라는 Scene 객체가 아니며 저장되지 않는다. 에디터가 RenderFrame의 view/projection을 편집 뷰포트에 맞게 바꾼다. Play에서는 게임 카메라를 쓰고 Stop은 실행 직전 편집 상태를 복원한다. Scene 렌더 타깃의 크기는 창의 백버퍼와 별개다.

SceneIO는 배치·구성만 저장한다. 새 콘텐츠 컴포넌트는 명시적 직렬화 분기를 추가하거나 별도 콘텐츠 설정 파일로 관리한다. Parameters의 최소/최대 범위는 콘텐츠에서 정의하고 Inspector는 그 범위를 따른다. 현재 전투 HP·Stagger·입력 예약을 씬 저장에 포함하지 않는다.

권장 재시작 순서는 입력/공격 중단 → Events::Reset → CollisionWorld::Reset → Transition::Reset → 이전 Scene 정리 → 새 객체와 구독 구성이다. 이전 epoch의 이벤트나 token의 완료 신호는 거부된다. 플레이어·보스 HP/게이지/쿨다운 초기값은 콘텐츠가 명시적으로 설정한다. 재시작을 1번 배로 연결하는 정책도 보스 콘텐츠가 소유한다.

## 실제 시연까지 남은 연결

- 플레이어 행동·콤보·회피·패링·강화·R·HUD와 타격 시각 검증.
- 공용 Stagger·약점 노출/파괴/복귀·공격체·동시 결과 우선순위.
- 세 구역 데이터·실제 준비 완료 조건·사망/실패/재시작 UI, 두 차례 전환.
- Sherlock 렌더 포팅·최종 재질/텍스처·조명/환경·그림자와 스키닝 동기화.
- 실제 캐릭터/촉수의 모든 모션·원본 외형 대조, 오디오 담당 및 연결.

각 항목은 엔진 기반 완료와 분리해 통합 합격을 기록한다. 현재 샌드박스의 도형과 전환 미리보기를 11월 핵심 전투 시연 또는 12월 완주 합격으로 취급하지 않는다.
