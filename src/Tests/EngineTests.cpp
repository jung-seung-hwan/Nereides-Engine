#include "Tests/EngineTests.h"
#include "Core/Input.h"
#include "Core/Time.h"
#include "Core/Log.h"
#include "Scene/Scene.h"
#include "Graphics/RenderFrame.h"
#include <functional>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace nereides
{
namespace
{
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool Near(double a, double b) { return std::abs(a - b) < 0.00001; }
void TestInput()
{
    Input input;
    input.BeginFrame(); input.SetKey('W', true); input.SetKey('W', true);
    Require(input.Pressed('W') && input.Held('W'), "input press and repeat");
    input.BeginFrame(); Require(!input.Pressed('W') && input.Held('W'), "input edge lifetime");
    input.SetKey('W', false); Require(input.Released('W') && !input.Held('W'), "input release");
    input.SetKey('W', true); input.SetFocus(false); input.SetFocus(true); input.SetKey('W', true);
    Require(!input.Held('W') && !input.Pressed('W'), "focus must not replay held action");
    input.SetKey('W', false); input.SetKey('W', true); Require(input.Pressed('W'), "fresh press");
    input.DiscardHeld(); input.SetKey('W', true); Require(!input.Pressed('W'), "transition input");
    input.SetKey(999, true); Require(!input.Held(999), "invalid key");
    input.BeginFrame(); input.SetKey('E', true); input.SetKey('E', false);
    Require(input.Pressed('E') && input.Released('E') && !input.Held('E'), "quick tap in one frame");
}
void TestTime()
{
    Time time; time.SetScale(0.25); time.Advance(4);
    Require(Near(time.combat.elapsed, 1) && Near(time.escape.elapsed, 4), "escape ignores slow motion");
    time.paused = true; time.Advance(2);
    Require(Near(time.combat.elapsed, 1) && Near(time.escape.elapsed, 4), "pause freezes game clocks");
    time.paused = false; time.combatStopped = true; time.Advance(3);
    Require(Near(time.presentation.elapsed, 7) && Near(time.combat.elapsed, 1), "presentation clock");
    time.Advance(-1); time.Advance(std::numeric_limits<double>::infinity());
    Require(Near(time.real.elapsed, 9), "invalid time"); Require(!time.SetScale(-1), "negative scale");
    time.Reset(); Require(time.frame == 0 && time.real.elapsed == 0, "restart clocks");
}
class TestAction final : public Component
{
public:
    explicit TestAction(std::function<void(Object&, Scene&)> action) : action(std::move(action)) {}
    void Update(Object& object, Scene& scene, const FrameContext&) override { action(object, scene); }
    std::function<void(Object&, Scene&)> action;
};
void TestScene()
{
    Scene scene; Input input; Time time;
    const auto parent = scene.Create("parent").Id();
    const auto child = scene.Create("child").Id();
    scene.Find(parent)->transform.position = {10,0,0};
    scene.Find(child)->transform.position = {2,0,0};
    Require(scene.SetParent(child, parent) && !scene.SetParent(parent, child), "parent cycles rejected");
    DirectX::XMFLOAT4X4 world; DirectX::XMStoreFloat4x4(&world, scene.World(child));
    Require(Near(world._41,12), "parent world transform");
    scene.Find(parent)->enabled = false; Require(!scene.Active(child), "inactive parent");
    scene.Find(parent)->enabled = true;
    int called = 0;
    scene.Find(parent)->Add<TestAction>([child](Object&, Scene& s) { s.Destroy(child); });
    scene.Find(child)->Add<TestAction>([&](Object&, Scene&) { ++called; });
    scene.Update({input,time}); Require(called == 0 && !scene.Find(child), "deleted peer must not update");
    scene.Clear(); Require(!scene.Find(parent), "old handle invalid after clear");
    auto& spawner = scene.Create("spawner");
    Require(spawner.Id() != parent, "object identifiers not reused");
    spawner.Add<TestAction>([&](Object& self, Scene& s) {
        s.Create("next frame").Add<TestAction>([&](Object&, Scene&) { ++called; });
        self.Remove<TestAction>();
    });
    scene.Update({input,time}); Require(called == 0, "new object waits a frame");
    scene.Update({input,time}); Require(called == 1, "deferred object updates and component removed");
    scene.Clear();
    auto& resetter = scene.Create("resetter");
    resetter.Add<TestAction>([](Object&, Scene& s) { s.Clear(); });
    scene.Update({input,time}); Require(scene.Objects().empty(), "clear during update is safe");
    auto& cameraObject = scene.Create("camera"); cameraObject.transform.position = {0,0,-5};
    Camera camera;
    DirectX::XMStoreFloat4x4(&world, camera.View(scene, cameraObject.Id()));
    Require(Near(world._43,5), "camera inverse world");
    bool rejected = false; try { camera.Projection(0); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "invalid camera aspect");
    scene.Destroy(cameraObject.Id()); scene.Flush();
}
void TestRenderInput()
{
    Scene scene; Camera camera;
    const auto cameraId=scene.Create("camera").Id();
    auto mesh=MakeCube(); std::weak_ptr<const MeshData> weak=mesh;
    const auto first=scene.Create("first").Id();
    scene.Find(first)->Add<MeshComponent>(mesh);
    const auto second=scene.Create("second").Id();
    scene.Find(second)->Add<MeshComponent>(mesh); scene.Find(second)->transform.position.x=4;
    auto frame=CollectRenderFrame(scene,cameraId,camera,1,0);
    Require(frame.items.size()==2 && Near(frame.items[1].world._41,4),"render world transforms");
    scene.Destroy(first); scene.Flush(); mesh.reset();
    Require(!weak.expired(),"shared model survives one object removal");
    scene.Clear(); Require(!weak.expired(),"frame keeps model alive");
    frame.items.clear(); Require(weak.expired(),"model released after final frame");
}
}
int RunEngineTests()
{
    try { TestInput(); TestTime(); TestScene(); TestRenderInput(); Log::Write(LogLevel::Info, "Engine tests PASS: input, time, scene, render input"); return 0; }
    catch (const std::exception& error) { Log::Write(LogLevel::Error, error.what()); return 1; }
}
}
