#include "Presentation/Transition.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <stdexcept>
namespace nereides
{
namespace
{
std::atomic<std::uint64_t> nextToken{1};
void Validate(const Clip2D& clip)
{
    for (double value : {clip.duration, clip.fadeOut, clip.fadeIn})
        if (!std::isfinite(value) || value < 0 || value > 600)
            throw std::invalid_argument("Invalid presentation duration");
    if (clip.layers.size() > 128)
        throw std::invalid_argument("Too many presentation layers");
    for (const auto& layer : clip.layers)
    {
        for (float value : {layer.from.x, layer.from.y, layer.to.x, layer.to.y, layer.size.x,
                            layer.size.y, layer.color.x, layer.color.y, layer.color.z,
                            layer.color.w, layer.startScale, layer.endScale})
            if (!std::isfinite(value))
                throw std::invalid_argument("Nonfinite presentation layer");
        if (layer.size.x <= 0 || layer.size.y <= 0 || layer.startScale <= 0 || layer.endScale <= 0)
            throw std::invalid_argument("Invalid presentation layer size");
    }
}
} // namespace
Clip2D Clip2D::Load(const std::filesystem::path& path)
{
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw std::runtime_error("Presentation file exceeds 1 MiB");
    std::ifstream input(path);
    nlohmann::json file;
    input >> file;
    if (file.at("version") != 1)
        throw std::runtime_error("Unsupported presentation version");
    Clip2D clip;
    clip.duration = file.at("duration");
    clip.fadeOut = file.at("fadeOut");
    clip.fadeIn = file.at("fadeIn");
    for (const auto& value : file.at("layers"))
    {
        Layer2D layer;
        layer.label = value.value("label", "");
        layer.texture = value.value("texture", "");
        if (!layer.texture.empty())
            layer.texture = (path.parent_path() / layer.texture).lexically_normal().string();
        const auto from = value.at("from").get<std::vector<float>>(),
                   to = value.at("to").get<std::vector<float>>(),
                   size = value.at("size").get<std::vector<float>>(),
                   color = value.at("color").get<std::vector<float>>();
        if (from.size() != 2 || to.size() != 2 || size.size() != 2 || color.size() != 4)
            throw std::runtime_error("Invalid layer vector size");
        layer.from = {from[0], from[1]};
        layer.to = {to[0], to[1]};
        layer.size = {size[0], size[1]};
        layer.color = {color[0], color[1], color[2], color[3]};
        layer.startScale = value.value("startScale", 1.f);
        layer.endScale = value.value("endScale", 1.f);
        clip.layers.push_back(std::move(layer));
    }
    Validate(clip);
    return clip;
}
void Transition::SetState(TransitionState state)
{
    m_state = state;
    m_time = 0;
    m_notices.push_back({m_token, state});
}
std::uint64_t Transition::Begin(Clip2D clip)
{
    Validate(clip);
    m_clip = std::move(clip);
    m_token = nextToken.fetch_add(1);
    m_notices.clear();
    m_error.clear();
    SetState(TransitionState::FadeOut);
    return m_token;
}
void Transition::Advance(double dt)
{
    if (!std::isfinite(dt) || dt < 0)
        return;
    // At most fade-out, clip, and fade-in can finish in one call. Waiting never consumes readiness.
    for (unsigned step = 0; step < 3; ++step)
    {
        double duration;
        if (m_state == TransitionState::FadeOut)
            duration = m_clip.fadeOut;
        else if (m_state == TransitionState::Playing)
            duration = m_clip.duration;
        else if (m_state == TransitionState::FadeIn)
            duration = m_clip.fadeIn;
        else
            return;
        const double remaining = duration - m_time;
        if (m_time + dt < duration)
        {
            m_time += dt;
            return;
        }
        dt = std::max(0., dt - remaining);
        if (m_state == TransitionState::FadeOut)
            SetState(TransitionState::Playing);
        else if (m_state == TransitionState::Playing)
            SetState(TransitionState::WaitingForScene);
        else
            SetState(TransitionState::Complete);
    }
}
bool Transition::Skip(std::uint64_t token)
{
    if (token != m_token || m_state != TransitionState::Playing)
        return false;
    SetState(TransitionState::WaitingForScene);
    return true;
}
bool Transition::SceneReady(std::uint64_t token)
{
    if (token != m_token || m_state != TransitionState::WaitingForScene)
        return false;
    SetState(TransitionState::FadeIn);
    return true;
}
bool Transition::Fail(std::uint64_t token, std::string reason)
{
    if (token != m_token || !BlocksCombat() || m_state == TransitionState::Failed)
        return false;
    m_error = std::move(reason);
    SetState(TransitionState::Failed);
    return true;
}
void Transition::Reset()
{
    m_token = nextToken.fetch_add(1);
    m_notices.clear();
    m_error.clear();
    m_clip = {};
    SetState(TransitionState::Idle);
}
float Transition::Fade() const
{
    if (m_state == TransitionState::FadeOut)
        return m_clip.fadeOut > 0 ? float(m_time / m_clip.fadeOut) : 1.f;
    if (m_state == TransitionState::FadeIn)
        return m_clip.fadeIn > 0 ? float(1 - m_time / m_clip.fadeIn) : 0.f;
    if (m_state == TransitionState::WaitingForScene || m_state == TransitionState::Failed)
        return 1;
    return 0;
}
} // namespace nereides
