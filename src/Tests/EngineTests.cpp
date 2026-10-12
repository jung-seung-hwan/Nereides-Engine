#include "Tests/EngineTests.h"
#include "Core/Input.h"
#include "Core/Time.h"
#include "Core/Log.h"
#include "Scene/Scene.h"
#include "Graphics/RenderFrame.h"
#include "Resource/Model.h"
#include "Animation/Timeline.h"
#include "Graphics/D3D11Renderer.h"
#include "Platform/Win32Window.h"
#include "Collision/Collision.h"
#include "Data/SceneIO.h"
#include "Presentation/Transition.h"
#include "Core/Events.h"
#include "Graphics/TextureCache.h"
#include "Editor/EditHistory.h"
#include "Editor/EditorGeometry.h"
#include "Editor/Console.h"
#include <fstream>
#include <functional>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nereides
{
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}


bool Near(double a, double b)
{
    return std::abs(a - b) < 0.00001;
}


void TestInput()
{
    Input input;
    input.BeginFrame();
    input.SetKey('W', true);
    input.SetKey('W', true);
    Require(input.Pressed('W') && input.Held('W'), "input press and repeat");
    input.BeginFrame();
    Require(!input.Pressed('W') && input.Held('W'), "input edge lifetime");
    input.SetKey('W', false);
    Require(input.Released('W') && !input.Held('W'), "input release");
    input.SetKey('W', true);
    input.SetFocus(false);
    input.SetFocus(true);
    input.SetKey('W', true);
    Require(!input.Held('W') && !input.Pressed('W'), "focus must not replay held action");
    input.SetKey('W', false);
    input.SetKey('W', true);
    Require(input.Pressed('W'), "fresh press");
    input.DiscardHeld();
    input.SetKey('W', true);
    Require(!input.Pressed('W'), "transition input");
    input.SetKey(999, true);
    Require(!input.Held(999), "invalid key");
    input.BeginFrame();
    input.SetKey('E', true);
    input.SetKey('E', false);
    Require(input.Pressed('E') && input.Released('E') && !input.Held('E'),
            "quick tap in one frame");
}


class UpdateProbe final : public Component
{
public:
    void Update(Object&, Scene&, const FrameContext& frame) override
    {
        ++frames;
        frameSeconds += frame.time.real.delta;
        framePresses += frame.input.Pressed('E');
        Require(frame.time.combat.delta == 0 && frame.time.escape.delta == 0,
                "frame callback cannot reuse fixed delta");
    }

    void FixedUpdate(Object&, Scene&, const FrameContext& frame) override
    {
        ++ticks;
        combatSeconds += frame.time.combat.delta;
        tickPresses += frame.input.Pressed('E');
    }

    unsigned frames = 0, ticks = 0, framePresses = 0, tickPresses = 0;
    double frameSeconds = 0, combatSeconds = 0;
};


void TestFixedUpdate()
{
    for (unsigned fps : {30u, 60u, 144u})
    {
        FixedStep fixed;
        Time time;
        Input input;
        Scene scene;
        auto& probe = scene.Create("update cadence").Add<UpdateProbe>();
        for (unsigned frame = 0; frame < fps; ++frame)
        {
            input.BeginFrame();
            if (frame == 0)
                input.SetKey('E', true);
            time.AdvanceFrame(1.0 / fps);
            const auto steps = fixed.Accumulate(1.0 / fps);
            for (unsigned step = 0; step < steps; ++step)
            {
                const auto tickInput = input.ConsumeTick();
                time.AdvanceTick(fixed.Step());
                scene.FixedUpdate({tickInput, time});
            }
            const auto frameTime = time.FrameView();
            scene.Update({input, frameTime});
        }
        Require(time.tick == 50 && time.frame == fps && Near(time.combat.elapsed, 1),
                "fixed update independent of render rate");
        Require(probe.frames == fps && probe.ticks == 50 && Near(probe.frameSeconds, 1) &&
                    Near(probe.combatSeconds, 1) && probe.framePresses == 1 &&
                    probe.tickPresses == 1,
                "independent callbacks and input channels");
        time.paused = true;
        time.AdvanceFrame(.01);
        const auto pausedFrame = time.FrameView();
        scene.Update({input, pausedFrame});
        Require(probe.frames == fps + 1 && probe.ticks == 50 && pausedFrame.presentation.delta == 0,
                "paused frame callback without tick");
    }
    FixedStep fixed;
    Require(fixed.Accumulate(.015) == 0 && fixed.Accumulate(.015) == 1 &&
                fixed.Accumulate(.05) == 3,
            "fixed update remainder");
    Require(fixed.Accumulate(1) == FixedStep::MaxStepsPerFrame &&
                Near(fixed.DroppedSeconds(), .84) && fixed.Accumulate(.001) == 0,
            "catch up limit discards whole step debt");
    fixed.Reset();
    Require(fixed.Accumulate(.019) == 0, "resume clears accumulated time");
    Require(fixed.Accumulate(-1) == 0 &&
                fixed.Accumulate(std::numeric_limits<double>::infinity()) == 0,
            "invalid fixed frame delta");
    FixedStep sixty(60);
    Require(sixty.Accumulate(1.0 / 60) == 1 && Near(sixty.Step(), 1.0 / 60),
            "configurable fixed frequency");

    Input input;
    input.SetKey('W', true);
    input.AddMouseDelta(3, 4);
    input.AddWheel(120);
    input.BeginFrame(); // No tick ran in the previous render frame.
    input.AddMouseDelta(2, -1);
    auto first = input.ConsumeTick();
    auto second = input.ConsumeTick();
    Require(first.Pressed('W') && first.Held('W') && first.MouseX() == 5 && first.MouseY() == 3 &&
                first.Wheel() == 120,
            "pending input survives render frames");
    Require(!second.Pressed('W') && second.Held('W') && second.MouseX() == 0 && second.Wheel() == 0,
            "catch up must not repeat input edges");
    input.SetKey('E', true);
    input.SetKey('E', false);
    input.BeginFrame();
    auto tap = input.ConsumeTick();
    Require(tap.Pressed('E') && tap.Released('E') && !tap.Held('E'),
            "quick tap survives until next tick");
    input.SetKey('Q', true);
    input.AddMouseDelta(1, 1);
    input.Capture(true, true);
    auto captured = input.ConsumeTick();
    Require(!captured.Pressed('Q') && !captured.Held('Q') && captured.MouseX() == 0,
            "editor capture clears pending tick input");
    input.SetKey('A', true);
    input.SetFocus(false);
    input.SetFocus(true);
    Require(!input.ConsumeTick().Pressed('A'), "focus clears pending tick input");
}


void TestTime()
{
    Time time;
    time.SetScale(0.25);
    time.Advance(4);
    Require(Near(time.combat.elapsed, 1) && Near(time.escape.elapsed, 4),
            "escape ignores slow motion");
    time.paused = true;
    time.Advance(2);
    Require(Near(time.combat.elapsed, 1) && Near(time.escape.elapsed, 4),
            "pause freezes game clocks");
    time.paused = false;
    time.combatStopped = true;
    time.Advance(3);
    Require(Near(time.presentation.elapsed, 7) && Near(time.combat.elapsed, 1),
            "presentation clock");
    time.Advance(-1);
    time.Advance(std::numeric_limits<double>::infinity());
    Require(Near(time.real.elapsed, 9), "invalid time");
    Require(!time.SetScale(-1), "negative scale");
    time.Reset();
    Require(time.frame == 0 && time.real.elapsed == 0, "restart clocks");
}


class TestAction final : public Component
{
public:
    explicit TestAction(std::function<void(Object&, Scene&)> action) : action(std::move(action)) {}
    void Update(Object& object, Scene& scene, const FrameContext&) override
    {
        action(object, scene);
    }

    void FixedUpdate(Object& object, Scene& scene, const FrameContext&) override
    {
        action(object, scene);
    }

    std::function<void(Object&, Scene&)> action;
};
class PhaseRecorder final : public Component
{
public:
    PhaseRecorder(UpdatePhase phase, std::vector<UpdatePhase>& order) : phase(phase), order(order)
    {
    }

    UpdatePhase Phase() const override
    {
        return phase;
    }

    void Update(Object&, Scene&, const FrameContext&) override
    {
        order.push_back(phase);
    }

private:
    UpdatePhase phase;
    std::vector<UpdatePhase>& order;
};


void TestScene()
{
    {
        Scene deferred;
        Input input;
        Time time;
        Events events;
        auto lifetime = std::make_shared<int>(0);
        std::weak_ptr<int> weak = lifetime;
        auto& object = deferred.Create("deferred destruction");
        const auto id = object.Id();
        object.Add<TestAction>(
            [lifetime](Object& self, Scene& scene) { scene.Destroy(self.Id()); });
        lifetime.reset();
        deferred.FixedUpdate({input, time}, false);
        Require(!deferred.Find(id) && !weak.expired(), "destroy hides before final flush");
        bool dispatched = false;
        events.Subscribe(0, "result", [&](const Event&) {
            Require(!weak.expired(), "events run before destruction");
            dispatched = true;
        });
        Event result;
        result.name = "result";
        result.epoch = events.Epoch();
        events.Publish(result);
        events.Dispatch(deferred);
        deferred.Flush();
        Require(dispatched && weak.expired(), "tick flush releases destroyed components");
    }
    Scene scene;
    Input input;
    Time time;
    const auto parent = scene.Create("parent").Id();
    const auto child = scene.Create("child").Id();
    scene.Find(parent)->transform.position = {10, 0, 0};
    scene.Find(child)->transform.position = {2, 0, 0};
    Require(scene.SetParent(child, parent) && !scene.SetParent(parent, child),
            "parent cycles rejected");
    DirectX::XMFLOAT4X4 world;
    DirectX::XMStoreFloat4x4(&world, scene.World(child));
    Require(Near(world._41, 12), "parent world transform");
    scene.Find(parent)->enabled = false;
    Require(!scene.Active(child), "inactive parent");
    scene.Find(parent)->enabled = true;
    int called = 0;
    scene.Find(parent)->Add<TestAction>([child](Object&, Scene& s) { s.Destroy(child); });
    scene.Find(child)->Add<TestAction>([&](Object&, Scene&) { ++called; });
    scene.Update({input, time});
    Require(called == 0 && !scene.Find(child), "deleted peer must not update");
    scene.Clear();
    Require(!scene.Find(parent), "old handle invalid after clear");
    auto& spawner = scene.Create("spawner");
    Require(spawner.Id() != parent, "object identifiers not reused");
    spawner.Add<TestAction>([&](Object& self, Scene& s) {
        s.Create("next frame").Add<TestAction>([&](Object&, Scene&) { ++called; });
        self.Remove<TestAction>();
    });
    scene.Update({input, time});
    Require(called == 0, "new object waits a frame");
    scene.Update({input, time});
    Require(called == 1, "deferred object updates and component removed");
    scene.Clear();
    auto& resetter = scene.Create("resetter");
    resetter.Add<TestAction>([](Object&, Scene& s) { s.Clear(); });
    scene.Update({input, time});
    Require(scene.Objects().empty(), "clear during update is safe");
    auto& cameraObject = scene.Create("camera");
    cameraObject.transform.position = {0, 0, -5};
    Camera camera;
    DirectX::XMStoreFloat4x4(&world, camera.View(scene, cameraObject.Id()));
    Require(Near(world._43, 5), "camera inverse world");
    bool rejected = false;

    try
    {
        camera.Projection(0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    Require(rejected, "invalid camera aspect");
    scene.Destroy(cameraObject.Id());
    scene.Flush();
    std::vector<UpdatePhase> order;
    scene.Create("animation first in storage").Add<PhaseRecorder>(UpdatePhase::Animation, order);
    scene.Create("logic second in storage").Add<PhaseRecorder>(UpdatePhase::Logic, order);
    scene.Create("late").Add<PhaseRecorder>(UpdatePhase::Late, order);
    scene.Update({input, time});
    Require(order == std::vector<UpdatePhase>{UpdatePhase::Logic, UpdatePhase::Animation,
                                              UpdatePhase::Late},
            "global update phases do not depend on object creation order");
}


void TestRenderInput()
{
    Scene scene;
    Camera camera;
    const auto cameraId = scene.Create("camera").Id();
    auto mesh = MakeCube();
    std::weak_ptr<const MeshData> weak = mesh;
    const auto first = scene.Create("first").Id();
    scene.Find(first)->Add<MeshComponent>(mesh);
    const auto second = scene.Create("second").Id();
    scene.Find(second)->Add<MeshComponent>(mesh);
    scene.Find(second)->transform.position.x = 4;
    auto frame = CollectRenderFrame(scene, cameraId, camera, 1, 0);
    Require(frame.items.size() == 2 && Near(frame.items[1].world._41, 4),
            "render world transforms");
    scene.Destroy(first);
    scene.Flush();
    mesh.reset();
    Require(!weak.expired(), "shared model survives one object removal");
    scene.Clear();
    Require(!weak.expired(), "frame keeps model alive");
    frame.items.clear();
    Require(weak.expired(), "model released after final frame");
}


void TestAnimation()
{
    auto model = std::make_shared<ModelData>();
    model->nodes.push_back({"root", -1, {}});
    AnimationClip clip;
    clip.name = "linear";
    clip.duration = 2;
    NodeTrack track;
    track.node = 0;
    track.positions = {{0, {0, 0, 0}}, {2, {4, 0, 0}}};
    clip.tracks.push_back(track);
    model->clips.push_back(clip);
    AnimationPlayer player(model);
    Require(player.Play(0, false), "play valid clip");
    player.Advance(1);
    Require(Near(player.LocalPose()[0].position.x, 2), "animation interpolation");
    player.Advance(8);
    Require(player.Finished() && Near(player.LocalPose()[0].position.x, 4),
            "clip completion clamps");
    player.Play(0, true, 1);
    player.Advance(.5);
    Require(Near(player.LocalPose()[0].position.x, 2.5), "snapshot crossfade");
    Require(!player.Play(90, false), "missing animation rejected");
    player.Play(0, false, 4);
    player.Advance(2);
    const float midBlend = player.LocalPose()[0].position.x;
    player.Advance(2);
    Require(player.Finished() && Near(player.LocalPose()[0].position.x, 4) && midBlend < 4,
            "long blend reaches endpoint after clip end");
    Timeline events;
    events.Start(1, {{0, "begin"}, {.2, "hit on"}, {.4, "hit off"}, {1, "end"}});
    Require(events.Advance(.8).size() == 3, "low FPS crosses all event markers");
    Require(events.Advance(1).size() == 1 && events.Advance(1).empty(),
            "completion event exactly once");
    events.Start(1, {{.5, "hit"}});
    const auto generation = events.Generation();
    events.Cancel();
    Require(events.Advance(1).empty() && events.Generation() != generation,
            "cancel invalidates action events");
    TransformKey a, b;
    b.time = 1;
    b.value.position.x = 10;
    Require(Near(EvaluateTrajectory({a, b}, .25).position.x, 2.5), "independent sword trajectory");
}


void TestModelCache()
{
    std::filesystem::create_directories("logs");
    const auto path = std::filesystem::path("logs/test-model.obj");
    {
        std::ofstream file(path);
        file << "o triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    ModelCache cache;
    auto first = cache.Load(path), second = cache.Load(path);
    Require(first == second && !first->parts.empty(), "model cache shares owned import data");
    Require(first->parts[0].mesh->vertices.size() == 3,
            "import data survives importer destruction");
    std::weak_ptr<const ModelData> weak = first;
    first.reset();
    second.reset();
    cache.Prune();
    Require(weak.expired(), "cache does not retain unused model");
    std::filesystem::remove(path);
}


void TestCollision()
{
    Scene scene;
    CollisionWorld collision;
    auto& player = scene.Create("point player");
    const auto playerId = player.Id();
    player.transform.position = {-2, 0, 0};
    auto& pc = player.Add<Collider>();
    pc.radius = 0;
    pc.layer = 1;
    pc.mask = 2;
    auto& trigger = scene.Create("escape");
    auto& tc = trigger.Add<Collider>();
    tc.shape = Shape::Box;
    tc.halfExtents = {.5f, .5f, .5f};
    tc.trigger = true;
    tc.layer = 2;
    tc.mask = 1;
    Require(collision.Step(scene, 0, 0).empty(), "no initial overlap");
    player.transform.position.x = 2;
    const auto contacts = collision.Step(scene, 0, 10);
    Require(contacts.size() == 2 && contacts[0].kind == ContactKind::Enter &&
                Near(contacts[0].time, 3.75),
            "swept trigger exact entry and exit");
    const double deadline = 3.75;
    Require(contacts[0].time <= deadline && contacts[0].time > deadline - .001,
            "deadline tie uses contact time");
    auto ray = collision.Raycast(scene, {-2, 0, 0}, {1, 0, 0}, 10, 2);
    Require(ray && Near(ray->distance, 1.5), "ray box nearest distance");
    player.transform.position = {.59f, .59f, 0};
    pc.radius = .1f;
    collision.Reset();
    Require(collision.Step(scene, 0, 0).empty(),
            "sphere box corner excludes inflated-box false positive");
    player.transform.position = {-5, 0, 0};
    pc.radius = .1f;
    tc.shape = Shape::Sphere;
    tc.radius = .1f;
    tc.trigger = false;
    collision.Reset();
    collision.Step(scene, 0, 0);
    player.transform.position = {5, 0, 0};
    const auto hit = collision.Step(scene, 0, 1);
    Require(hit.size() == 1 && Near(hit[0].time, .48), "fast sphere sweep avoids tunneling");
    scene.Destroy(playerId);
    scene.Flush();
    Require(collision.Step(scene, 1, 2).empty(), "removed collider emits no hit");
    HitLedger ledger;
    Require(ledger.Record(7, 9) && !ledger.Record(7, 9) && ledger.Record(7, 10),
            "attack target duplicate policy");
    ledger.End(7);
    Require(ledger.Record(7, 9), "attack cleanup");
}


void TestSceneIO()
{
    Scene scene;
    ModelCache cache;
    auto camera = scene.Create("camera").Id();
    auto player = scene.Create("player").Id();
    auto* object = scene.Find(player);
    object->transform.position = {1, 2, 3};
    object->Add<MeshComponent>(MakeCube());
    object->Add<Collider>().radius = 2;
    object->Add<Parameters>().values["duration"] = {3, .1, 10};
    scene.SetParent(player, camera);
    const std::filesystem::path path = "logs/test-scene.json";
    SceneIO::Save(scene, camera, player, path);
    auto loaded = SceneIO::Load(path, cache);
    Require(loaded.scene.Objects().size() == 2 && loaded.player != player,
            "load remaps object handles");
    Require(loaded.scene.Find(loaded.player)->Parent() == loaded.camera, "load restores hierarchy");
    Require(Near(loaded.scene.Find(loaded.player)->transform.position.z, 3),
            "load restores transform");
    Require(
        Near(loaded.scene.Find(loaded.player)->Get<Parameters>()->values.at("duration").value, 3),
        "load restores parameters");
    SceneIO::Save(loaded.scene, loaded.camera, loaded.player,
                  path); // Atomic replacement of an existing file.
    loaded.scene.Find(loaded.player)->Get<Parameters>()->values["duration"].value = -1;
    bool invalidParameter = false;

    try
    {
        SceneIO::Save(loaded.scene, loaded.camera, loaded.player, path);
    }
    catch (const std::exception&)
    {
        invalidParameter = true;
    }

    auto preserved = SceneIO::Load(path, cache);
    Require(invalidParameter && Near(preserved.scene.Find(preserved.player)
                                         ->Get<Parameters>()
                                         ->values.at("duration")
                                         .value,
                                     3),
            "invalid save preserves previous file");
    {
        std::ofstream file(path);
        file << "{\"version\":999}";
    }

    bool failed = false;

    try
    {
        SceneIO::Load(path, cache);
    }
    catch (const std::exception&)
    {
        failed = true;
    }

    Require(failed && loaded.scene.Find(loaded.player), "failed load preserves live scene");
    std::filesystem::remove(path);
}


void TestConsoleRecords()
{
    const auto before = Log::Entries();
    const auto boundary = before.empty() ? 0 : before.back().id;
    const auto first = Log::Write(LogLevel::Error, "Repeated test: Missing 모델.FBX");
    Log::Write(LogLevel::Info, "Other test information");
    const auto last = Log::Write(LogLevel::Error, "Repeated test: Missing 모델.FBX");
    Log::Write(LogLevel::Warning, "Repeated test: Missing 모델.FBX");
    const auto entries = Log::Entries();
    auto grouped = FilterLogs(entries, boundary, 0, "missing", true, first);
    Require(grouped.size() == 2, "console groups by severity and message");
    Require(grouped[0].count == 2 && grouped[0].entry.id == last && grouped[0].focused,
            "repeat group retains latest timestamp and reveal target");
    Require(FilterLogs(entries, boundary, 3, "MODEL", false).empty(),
            "search preserves Unicode bytes");
    Require(FilterLogs(entries, boundary, 3, ".fbx", false).size() == 2,
            "severity and case-insensitive search combine");
    Require(FilterLogs(entries, last, 0, "", true).size() == 1,
            "clear boundary admits only later records");
    Require(entries.back().time.size() == 23 && first > boundary && last > first,
            "logs carry wall time and stable monotonic ids");
    Require(Log::Format(entries.back()).find("[Warning]") != std::string::npos,
            "clipboard and file format includes severity");
}


void TestEditorHistory()
{
    Scene scene;
    ModelCache cache;
    const auto camera = scene.Create("camera").Id();
    const auto player = scene.Create("player").Id();
    const auto parent = scene.Create("parent").Id();
    scene.Find(parent)->transform.position = {4, 0, 0};
    auto& child = scene.Create("child");
    const auto childId = child.Id();
    child.Add<MeshComponent>(MakeCube());
    child.transform.position = {0, 2, 3};
    scene.SetParent(childId, parent);
    EditHistory history;
    const auto initial = SceneIO::Encode(scene, camera, player);
    history.Reset(initial);
    auto decoded = SceneIO::Decode(initial, cache);
    Require(SceneIO::Encode(decoded.scene, decoded.camera, decoded.player) == initial,
            "history canonical form survives handle remapping");
    scene.Destroy(parent);
    scene.Flush();
    history.Commit(SceneIO::Encode(scene, camera, player));
    Require(history.Dirty() && history.CanUndo(), "deletion records an edit");
    decoded = SceneIO::Decode(history.Target(-1), cache);
    history.Accept(-1);
    Require(decoded.scene.Objects().size() == 4 && !history.Dirty(),
            "undo restores entire deleted subtree and saved state");
    auto restoredChild = decoded.scene.Objects().back();
    const auto box = EditorBounds(decoded.scene, restoredChild);
    Require(Near(box.Center.x, 4) && Near(box.Center.y, 2) && Near(box.Center.z, 3),
            "editor bounds include parent transform");
    Require(PickEditorObject(decoded.scene, DirectX::XMVectorSet(4, 2, -5, 1),
                             DirectX::XMVectorSet(0, 0, 1, 0)) == restoredChild,
            "viewport ray selects visible object");
    Require(!PickEditorObject(decoded.scene, DirectX::XMVectorSet(20, 2, -5, 1),
                              DirectX::XMVectorSet(0, 0, 1, 0)),
            "viewport ray misses empty space");
    decoded.scene.Find(restoredChild)->transform.position.z = 9;
    history.Commit(SceneIO::Encode(decoded.scene, decoded.camera, decoded.player));
    Require(!history.CanRedo(), "edit after undo discards the old redo branch");
    history.MarkSaved();
    Require(!history.Dirty(), "save marks history position");
    history.Accept(-1);
    Require(history.Dirty(), "undo away from saved state becomes dirty");
    history.Accept(1);
    Require(!history.Dirty(), "redo to saved state becomes clean");

    for (int i = 0; i < 100; ++i)
        history.Commit(std::to_string(i));
    unsigned count = 0;

    while (history.CanUndo())
    {
        history.Accept(-1);
        ++count;
    }

    Require(count == 64, "history bounded to 64 undo operations");
}


void TestTransitionsAndEvents()
{
    Transition playback;
    Clip2D clip;
    clip.duration = 2;
    clip.fadeOut = .5;
    clip.fadeIn = .5;
    auto token = playback.Begin(clip);
    Require(playback.BlocksCombat(), "presentation gates combat");
    playback.Advance(.5);
    Require(playback.State() == TransitionState::Playing, "fade out then 2D");
    Require(playback.Skip(token) && !playback.Skip(token), "skip once");
    playback.Advance(100);
    Require(playback.State() == TransitionState::WaitingForScene, "skip waits for scene");
    Require(!playback.SceneReady(token + 1) && playback.SceneReady(token), "readiness token");
    playback.Advance(.49);
    Require(playback.BlocksCombat(), "no early input restoration");
    playback.Advance(.01);
    Require(playback.State() == TransitionState::Complete && !playback.BlocksCombat(),
            "fade in completes");
    token = playback.Begin(clip);
    playback.Advance(100);
    Require(playback.State() == TransitionState::WaitingForScene,
            "natural completion converges with skip");
    Require(playback.Fail(token, "missing scene"), "prepare failure");
    Require(!playback.SceneReady(token), "failure not readiness");
    playback.Reset();
    Require(!playback.SceneReady(token) && !playback.BlocksCombat(),
            "restart rejects stale completion");
    Scene scene;
    auto owner = scene.Create("listener").Id();
    Events events;
    int received = 0;
    auto subscription = events.Subscribe(owner, "hit", [&](const Event&) { ++received; });
    Event event;
    event.name = "hit";
    event.epoch = events.Epoch();
    events.Publish(event);
    scene.Destroy(owner);
    scene.Flush();
    events.Dispatch(scene);
    Require(received == 0, "dead listener not invoked");
    events.Unsubscribe(subscription);
    events.Subscribe(0, "hit", [&](const Event&) {
        ++received;
        events.Reset();
    });
    events.Publish(event);
    events.Publish(event);
    events.Dispatch(scene);
    Require(received == 1 && !events.Publish(event),
            "restart invalidates queued and late old events");
    events.Subscribe(0, "hit", [&](const Event& e) {
        ++received;
        if (received == 2)
            events.Publish(e);
    });
    event.epoch = events.Epoch();
    events.Publish(event);
    events.Dispatch(scene);
    Require(received == 2, "reentrant publish waits next dispatch");
    events.Dispatch(scene);
    Require(received == 3, "queued next dispatch");
    const auto loaded = Clip2D::Load("data/presentation/preview.json");
    Require(loaded.layers.size() == 3, "presentation data loading");
    std::weak_ptr<const MeshData> released;

    for (int restart = 0; restart < 32; ++restart)
    {
        Scene cycle;
        CollisionWorld collision;
        Events notifications;
        Transition transition;
        auto& object = cycle.Create("restart actor");
        const auto id = object.Id();
        auto mesh = MakeCube();
        released = mesh;
        object.Add<MeshComponent>(mesh);
        object.Add<Collider>();
        mesh.reset();
        notifications.Subscribe(id, "hit", [&](const Event&) {
            throw std::runtime_error("Old restart callback executed");
        });
        Event pending;
        pending.name = "hit";
        pending.epoch = notifications.Epoch();
        notifications.Publish(pending);
        const auto old = transition.Begin(clip);
        collision.Step(cycle, 0, 0);
        notifications.Reset();
        collision.Reset();
        transition.Reset();
        cycle.Clear();
        notifications.Dispatch(cycle);
        Require(released.expired() && !cycle.Find(id) && !transition.SceneReady(old),
                "restart releases resources and old references");
    }
}
} // namespace


int RunEngineTests()
{
    try
    {
        TestInput();
        TestTime();
        TestFixedUpdate();
        TestScene();
        TestRenderInput();
        TestAnimation();
        TestModelCache();
        TestCollision();
        TestSceneIO();
        TestEditorHistory();
        TestConsoleRecords();
        TestTransitionsAndEvents();
        Log::Write(LogLevel::Info,
                   "Engine tests PASS: input, time, scene, render input, animation, model cache, "
                   "collision, scene IO, transitions, events");
        return 0;
    }
    catch (const std::exception& error)
    {
        Log::Write(LogLevel::Error, error.what());
        return 1;
    }
}


int RunModelTest(const std::filesystem::path& path)
{
    try
    {
        ModelCache cache;
        const auto model = cache.Load(path);
        Require(!model->parts.empty(), "model probe requires mesh parts");
        ModelComponent component(model);

        for (std::size_t clip = 0; clip < model->clips.size(); ++clip)
        {
            component.player.Play(clip, false);

            for (int sample = 0; sample < 5; ++sample)
            {
                component.player.Advance(model->clips[clip].duration / 4);
                RenderFrame frame;
                AppendModelDraws(component, DirectX::XMMatrixIdentity(), frame);
                Require(!frame.items.empty(), "model draws missing");

                for (const auto& item : frame.items)
                    for (const auto& bone : item.bones)
                        for (const auto& row : bone.m)
                            for (float value : row)
                                Require(std::isfinite(value), "nonfinite bone matrix");
            }
        }

        using namespace DirectX;
        Win32Window window;
        Require(window.Create(GetModuleHandleW(nullptr), SW_HIDE), "probe window");
        D3D11Renderer renderer;
        Require(renderer.Initialize(window.Handle(), window.Width(), window.Height()),
                "probe renderer");
        if (!model->clips.empty())
            component.player.Play(0, true);
        RenderFrame frame;
        AppendModelDraws(component, XMMatrixIdentity(), frame);
        XMFLOAT3 low{FLT_MAX, FLT_MAX, FLT_MAX}, high{-FLT_MAX, -FLT_MAX, -FLT_MAX};

        for (const auto& item : frame.items)
            for (const auto& vertex : item.mesh->vertices)
            {
                auto p = XMLoadFloat3(&vertex.position);
                if (!item.bones.empty())
                {
                    auto skinned = XMVectorZero();

                    for (unsigned i = 0; i < 4; ++i)
                        if (vertex.weights[i] > 0)
                            skinned += XMVector3TransformCoord(
                                           p, XMLoadFloat4x4(&item.bones.at(vertex.bones[i]))) *
                                       vertex.weights[i];
                    p = skinned;
                }

                p = XMVector3TransformCoord(p, XMLoadFloat4x4(&item.world));
                XMFLOAT3 point;
                XMStoreFloat3(&point, p);
                Require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z),
                        "nonfinite skinned position");
                low.x = std::min(low.x, point.x);
                low.y = std::min(low.y, point.y);
                low.z = std::min(low.z, point.z);
                high.x = std::max(high.x, point.x);
                high.y = std::max(high.y, point.y);
                high.z = std::max(high.z, point.z);
            }
        const auto center = (XMLoadFloat3(&low) + XMLoadFloat3(&high)) * .5f;
        const float radius = std::max(
            .01f, XMVectorGetX(XMVector3Length(XMLoadFloat3(&high) - XMLoadFloat3(&low))) * .5f);
        const auto eye = center + XMVectorSet(0, 0, -radius * 3, 0);
        XMStoreFloat4x4(&frame.view, XMMatrixLookAtLH(eye, center, XMVectorSet(0, 1, 0, 0)));
        XMStoreFloat4x4(&frame.projection,
                        XMMatrixPerspectiveFovLH(XM_PIDIV4, float(window.Width()) / window.Height(),
                                                 radius * .01f, radius * 10));
        std::uint64_t first = 0, second = 0;
        renderer.RequestCapture("logs/model-probe.bmp");
        Require(renderer.RenderScene(frame, false, &first), "model GPU visibility");
        TextureCache textures;
        textures.SetDevice(renderer.Device());
        auto* texture = textures.Get("logs/model-probe.bmp");
        Require(texture && textures.Get("logs/model-probe.bmp") == texture,
                "WIC texture upload and cache");
        if (!model->clips.empty())
        {
            component.player.Advance(model->clips[0].duration * .37);
            frame.items.clear();
            AppendModelDraws(component, XMMatrixIdentity(), frame);
            Require(renderer.RenderScene(frame, false, &second), "animated model GPU visibility");
            Log::Write(LogLevel::Info, std::string("GPU animation pixel change=") +
                                           (first != second ? "yes" : "no (inspect static clip)"));
        }

        Log::Write(LogLevel::Info, "Imported bounds extent " + std::to_string(high.x - low.x) +
                                       ", " + std::to_string(high.y - low.y) + ", " +
                                       std::to_string(high.z - low.z));
        Log::Write(LogLevel::Info, "Model probe PASS " + path.string());
        return 0;
    }
    catch (const std::exception& error)
    {
        Log::Write(LogLevel::Error, error.what());
        return 1;
    }
}
} // namespace nereides
