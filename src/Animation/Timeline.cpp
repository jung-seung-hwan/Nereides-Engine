#include "Animation/Timeline.h"
namespace nereides
{
Transform EvaluateTrajectory(const std::vector<TransformKey>& keys, double time)
{
    if (keys.empty() || !std::isfinite(time))
        throw std::invalid_argument("Invalid trajectory");
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (!std::isfinite(keys[i].time) || (i && keys[i].time <= keys[i - 1].time))
            throw std::invalid_argument("Unordered trajectory keys");
    if (time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;
    const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                       [](double t, const auto& k) { return t < k.time; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const float t = static_cast<float>((time - a.time) / (b.time - a.time));
    Transform result;
    DirectX::XMStoreFloat3(&result.position,
                           DirectX::XMVectorLerp(DirectX::XMLoadFloat3(&a.value.position),
                                                 DirectX::XMLoadFloat3(&b.value.position), t));
    DirectX::XMStoreFloat3(&result.scale,
                           DirectX::XMVectorLerp(DirectX::XMLoadFloat3(&a.value.scale),
                                                 DirectX::XMLoadFloat3(&b.value.scale), t));
    // Euler channels are authored as unwrapped angles; skeletal clips use quaternion slerp.
    DirectX::XMStoreFloat3(&result.rotation,
                           DirectX::XMVectorLerp(DirectX::XMLoadFloat3(&a.value.rotation),
                                                 DirectX::XMLoadFloat3(&b.value.rotation), t));
    return result;
}
} // namespace nereides
