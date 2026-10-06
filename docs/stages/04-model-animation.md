# 04 모델 리소스와 애니메이션

Assimp로 파일을 읽은 뒤 Mesh·노드 계층·재질 참조·뼈 바인딩·클립을 엔진 데이터로 복사한다. Assimp Importer가 해제돼도 데이터는 살아 있다. 모델 캐시는 정규화된 경로를 키로 약한 참조를 보관한다.

## 준비와 읽기 순서

최초 한 번 `scripts/setup-dependencies.ps1 -VcpkgRoot C:/vcpkg`처럼 각자의 vcpkg 경로를 전달한다. 프로젝트는 vcpkg.json의 baseline과 x64-windows-v143 triplet으로 버전을 고정한다. VS 솔루션 방식은 유지하며 CMake는 vcpkg 내부 라이브러리 빌드에만 사용한다. 다른 엔진의 설치 캐시를 참조하지 않는다.

1. `src/Resource/Model.h`: 모델의 노드·메시 파트·재질·클립·뼈 바인딩과 AnimationPlayer.
2. `src/Resource/Model.cpp`: Load의 파일 읽기 → 노드 → 재질 → 메시/가중치 → 클립. 이어 Play/Advance/Evaluate/GlobalPose.
3. `src/Animation/Timeline.h`: 한 공격의 이벤트를 시간 구간으로 꺼낸다. 낮은 FPS에서도 지나간 이벤트를 모두 반환하고 Cancel은 이전 세대를 무효화한다. 이벤트에 따른 판정 활성화는 콘텐츠가 수행한다.
4. `src/Animation/Timeline.cpp`: 공격 시간이 주어졌을 때 독립 검 궤적을 평가한다. 이동/크기/연속 Euler 채널 보간이며 뼈대 회전은 quaternion slerp다.
5. `AppendModelDraws` → `RenderFrame` → `MeshShaders.h`: CPU가 계산한 뼈 행렬을 검증용 GPU 스키닝 경로에 전달한다. Sherlock의 최종 메인/그림자 패스 포팅은 별도다.

## 구현 계약

FBX pivot 보존은 끄고 왼손 좌표 변환·삼각형화·최대 4개 가중치·GlobalScale을 적용한다. 노드 이름 중복은 모호한 뼈 매핑 대신 오류로 반환한다. 한 메시의 뼈는 127개와 무가중치 정점용 fallback 하나까지다. 클립 전환은 현재 로컬 포즈의 스냅샷에서 새 클립으로 블렌딩한다. 루트 이동은 포즈에 남으며 게임 객체 이동으로 추출하지 않는다.

재질은 기본색과 텍스처 경로를 전달한다. 현재 검증 렌더는 기본색만 표시한다. 임베디드 텍스처 해석·최종 텍스처/PBR·그림자·리타게팅·별도 모션 파일의 뼈대 매핑은 완료 범위가 아니다. 모든 FBX 호환을 보장하지 않으며 대표 시험 결과만 기록한다.

## 검증

`scripts/verify.ps1`: Debug/Release 빌드, 입력/시간/장면/렌더 수명/클립 보간/블렌딩/타임라인 취소/검 궤적/모델 캐시 테스트, 두 GPU smoke-test 통과.

추가로 실행 파일에 `--model-test assets/black_sword/model/black_sword_all_clips.fbx` 또는 `--model-test assets/kraken_tentacle2/game_v3/Kraken_Tentacle_8k_Slam_v3.fbx`를 전달한다. 경로에 공백이 있으면 경로를 따옴표로 감싼다. 시험은 모든 클립의 여러 시점에서 뼈 행렬 유효성을 확인하고 첫 클립의 두 자세를 GPU로 그려 픽셀 변화를 기록한다. 로컬 assets는 기존 정책대로 커밋하지 않는다.

2026-10-06 Debug에서 검은 5노드·1파트·7클립, 촉수는 28노드·1파트·2클립으로 읽혔다. 두 모델 모두 GPU 표시 성공과 애니메이션에 따른 픽셀 변화가 확인됐다. 검의 첫 자세 범위는 약 0.342 × 1.421 × 0.366m, 촉수는 약 12.843 × 9.991 × 2.041m였다. 제작 도구의 모든 포즈·재질과 육안 일치 검증까지 끝난 것은 아니다.

런타임 DLL과 의존 패키지의 copyright는 빌드 출력에 자동 복사한다. 설치 폴더는 Git에서 제외하고 manifest·triplet·프로젝트 연결만 공유한다.
