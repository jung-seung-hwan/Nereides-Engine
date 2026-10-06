#pragma once
#include "Scene/Scene.h"
#include <cmath>
namespace nereides
{
// Integration example; not the player's final action/camera controller.
class MoveComponent final : public Component
{
public:
    void Update(Object& object, Scene&, const FrameContext& frame) override
    {
        float x = float(frame.input.Held('D')) - float(frame.input.Held('A'));
        float z = float(frame.input.Held('W')) - float(frame.input.Held('S'));
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
