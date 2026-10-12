#pragma once
#include "Scene/Scene.h"
#include <cmath>

namespace nereides
{
// Integration example; not the player's final action/camera controller.
// 객체에 붙이는 이동용 컴포넌트
// 객체.Add<MoveComponent>()로 추가
// 전달받은 입력과 시간 정보에 따라서 객체의 위치 변경
class MoveComponent final : public Component
{
public:
    void FixedUpdate(Object& object, Scene&, const FrameContext& frame) override
    {
        // 키 입력을 방향으로 변경
        // Held()로 계속 누르는 상태 반영
        float x = float(frame.input.Held('D')) - float(frame.input.Held('A'));
        float z = float(frame.input.Held('W')) - float(frame.input.Held('S'));

        // 방향 길이를 구하고, 길이로 나누어 대각선 이동 속도 보정
        const float length = std::sqrt(x * x + z * z);

        if (length > 0)
        {
            const float step = speed * static_cast<float>(frame.time.combat.delta) / length;
            object.transform.position.x += x * step;
            object.transform.position.z += z * step;
        }
    }

    float speed = 3.f;
};
} // namespace nereides
