# 03 장면과 렌더 입력 연결

객체의 최종 월드 행렬·카메라·메시 참조를 `RenderFrame`으로 수집한다. 렌더러는 Scene이나 콘텐츠 행동을 직접 읽지 않는다. 지금의 DX11 구현은 연결 검증용이며 Sherlock 렌더 포팅 완료를 의미하지 않는다.

## 읽는 순서

1. `src/Graphics/RenderFrame.h`: CPU MeshData, 프레임별 DrawItem, MeshComponent.
2. `src/Graphics/RenderFrame.cpp`: 활성 객체를 수집하고 월드/뷰/투영 값을 복사한다. MakeCube는 외부 에셋 없이 사용하는 임시 도형이다.
3. `src/Graphics/D3D11Renderer.cpp`: Upload → RenderScene. 공유 메시의 GPU 버퍼를 재사용하고 인덱스·깊이 버퍼·상수 버퍼로 그린다.
4. `src/Graphics/MeshShaders.h`: 행 벡터의 world × view × projection. row_major 선언으로 C++과 같은 저장 규약을 쓴다.
5. `src/Core/Application.cpp`: 임시 갑판·플레이어·카메라 구성과 RenderFrame 전달.

## 실행

F5로 갑판과 황색 상자를 확인하고 WASD로 이동한다. Esc는 정지다. 지금 카메라는 고정 시점이며 최종 근접 카메라 동작은 콘텐츠 담당 범위다. 조명·텍스처·스키닝·그림자는 아직 이 경로에 없다.

## 수명

MeshComponent와 RenderFrame은 shared_ptr로 CPU 데이터를 유지한다. GPU 캐시는 weak_ptr로 원본 수명을 확인한다. 마지막 CPU 참조가 사라진 항목은 다음 프레임에서 제거하고, 렌더 종료 시 모두 정리한다. CPU 메시를 등록한 뒤 직접 수정하지 않고 새 immutable 데이터로 교체한다. 같은 주소가 재사용돼도 만료된 캐시는 재업로드한다. 파일 입력과 GPU 업로드 실패는 별개다.

장치·스왑체인은 D3D11Renderer 한 개가 소유한다. 창 크기 변경 때 깊이 버퍼도 다시 만든다. 에디터 오프스크린 타깃과 Sherlock 포팅은 후속 연결 작업이다.

## 검증 결과

2026-10-06 Debug/Release 빌드, 엔진 테스트, 기존 삼각형 `--smoke-test`, 새 3D 장면 `--scene-smoke-test` 모두 통과했다. 두 GPU 검사는 배경과 중앙 픽셀을 읽어 실제 출력 유무를 확인한다. 공유 객체 하나 제거, 프레임 종료까지 CPU 리소스 유지, 최종 참조 해제는 엔진 테스트로 확인했다. 전체 화면 육안 검증·성능 목표 달성을 의미하지 않는다.
