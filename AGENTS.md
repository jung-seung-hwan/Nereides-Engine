# Nereides Engine 작업 안내

작업 전에 다음 문서와 실제 변경 상태를 확인한다.

1. [프로젝트 안내](README.md): 목적, 폴더, 빌드·실행·검증 방법.
2. [공용 작업 규칙](docs/development/vibe-coding-rules.md): 작업 단위, 기준 유지, 검증·인계 방식.
3. [현재 작업 상태](docs/development/current-status.md): 현재 목표, 확정 사항, 남은 일과 다음 행동.
4. [구현 로드맵](docs/implementation-roadmap.md)과 해당 단계 문서: 기능별 구현·검증 기록.

프로젝트별 범위는 `docs/engine-scope.md`, API 계약은 `docs/integration-contracts.md`, 담당 경계와 Git 협업은 `docs/collaboration.md`를 따른다. 과거 문서의 브랜치·커밋·검증 결과는 당시 기록이므로 현재 Git 상태와 구분한다.

한 세션은 하나의 목표를 맡고 이름은 `[Nereides Engine] 작업 목적 · 01`을 기본으로 한다. 같은 목표의 조사·수정·검증은 함께 진행한다. 새 세션 생성과 외부 전송은 사용자의 요청 범위에 맞춰 수행한다.

확정된 결정과 다음 행동은 현재 작업 상태에 기록하고, 상세 구현·검증은 기존 단계 문서를 갱신한다. 반복 제작이 실제로 필요해지면 `docs/development/prompts/`에 용도별 기준과 버전을 기록하고 참조 자료 위치를 연결한다. 필요 전에는 빈 제작 폴더나 세션을 만들지 않는다.
