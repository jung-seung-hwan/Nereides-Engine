# Git 협업 안내

2026-10-06 현재 main의 기준 커밋은 54f096f이고 공통 엔진 구현은 codex/engine-foundation에서 진행한다. 단계마다 코드·검증·학습 문서를 함께 커밋하고 origin에 push한다. main 병합은 별도 통합 작업이다.

- 정승환: 공통 실행·객체·입력·시간·리소스·재생·충돌·편집·수명.
- 고성현: 렌더 포팅·GPU 자원·스키닝·조명·환경·효과와 화면 출력.
- 유상준: 플레이어 행동·카메라 동작·피해/방어·HUD.
- 김진우: 보스·촉수·공용 Stagger·약점·공격체·구역/승패/재시작.

[로드맵](implementation-roadmap.md)의 단계 문서와 [연결 안내](integration-contracts.md)를 보고 같은 계약으로 작업한다. 변경할 때 사용하는 담당자에게 이유·영향·사용 예제·검증 장면을 함께 전달한다.

.sln/.vcxproj/.filters, 소스, 의존성 manifest/triplet, 데이터 규격과 문서는 추적한다. bin/obj/.vs/vcpkg_installed/logs/captures/dist와 개인 설정은 제외한다. MMD와 assets는 각 팀원이 준비하며 이 구현 커밋에 넣지 않았다. 기존 기획 DOCX의 작업 중 변경도 별도로 유지했다.

통합 전 scripts/verify.ps1로 Debug/Release를 확인한다. 모델·콘텐츠를 바꾸면 해당 로컬 에셋 시험과 실제 장면을 추가한다. 자동 검사 통과를 전체 게임 합격으로 바꾸지 않는다. 기능별 임시값·미정 규칙·미검증 동작은 단계 문서에 남긴다.
