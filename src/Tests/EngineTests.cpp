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
#include <fstream>
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
void TestAnimation()
{
    auto model=std::make_shared<ModelData>(); model->nodes.push_back({"root",-1,{}});
    AnimationClip clip; clip.name="linear"; clip.duration=2;
    NodeTrack track; track.node=0; track.positions={ {0,{0,0,0}}, {2,{4,0,0}} };
    clip.tracks.push_back(track); model->clips.push_back(clip);
    AnimationPlayer player(model); Require(player.Play(0,false),"play valid clip"); player.Advance(1);
    Require(Near(player.LocalPose()[0].position.x,2),"animation interpolation");
    player.Advance(8); Require(player.Finished() && Near(player.LocalPose()[0].position.x,4),"clip completion clamps");
    player.Play(0,true,1); player.Advance(.5);
    Require(Near(player.LocalPose()[0].position.x,2.5),"snapshot crossfade");
    Require(!player.Play(90,false),"missing animation rejected");
    Timeline events; events.Start(1,{{0,"begin"},{.2,"hit on"},{.4,"hit off"},{1,"end"}});
    Require(events.Advance(.8).size()==3,"low FPS crosses all event markers");
    Require(events.Advance(1).size()==1 && events.Advance(1).empty(),"completion event exactly once");
    events.Start(1,{{.5,"hit"}}); const auto generation=events.Generation(); events.Cancel();
    Require(events.Advance(1).empty() && events.Generation()!=generation,"cancel invalidates action events");
    TransformKey a,b; b.time=1; b.value.position.x=10;
    Require(Near(EvaluateTrajectory({a,b},.25).position.x,2.5),"independent sword trajectory");
}
void TestModelCache()
{
    std::filesystem::create_directories("logs");
    const auto path=std::filesystem::path("logs/test-model.obj");
    { std::ofstream file(path); file<<"o triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; }
    ModelCache cache; auto first=cache.Load(path),second=cache.Load(path);
    Require(first==second && !first->parts.empty(),"model cache shares owned import data");
    Require(first->parts[0].mesh->vertices.size()==3,"import data survives importer destruction");
    std::weak_ptr<const ModelData> weak=first; first.reset(); second.reset(); cache.Prune();
    Require(weak.expired(),"cache does not retain unused model");
    std::filesystem::remove(path);
}
}
int RunEngineTests()
{
    try { TestInput(); TestTime(); TestScene(); TestRenderInput(); TestAnimation(); TestModelCache(); Log::Write(LogLevel::Info, "Engine tests PASS: input, time, scene, render input, animation, model cache"); return 0; }
    catch (const std::exception& error) { Log::Write(LogLevel::Error, error.what()); return 1; }
}
int RunModelTest(const std::filesystem::path& path)
{
    try
    {
        ModelCache cache; const auto model=cache.Load(path);
        Require(!model->parts.empty(),"model probe requires mesh parts");
        ModelComponent component(model);
        for(std::size_t clip=0;clip<model->clips.size();++clip)
        {
            component.player.Play(clip,false);
            for(int sample=0;sample<5;++sample)
            {
                component.player.Advance(model->clips[clip].duration/4);
                RenderFrame frame; AppendModelDraws(component,DirectX::XMMatrixIdentity(),frame);
                Require(!frame.items.empty(),"model draws missing");
                for(const auto& item:frame.items)
                    for(const auto& bone:item.bones)
                        for(const auto& row:bone.m) for(float value:row) Require(std::isfinite(value),"nonfinite bone matrix");
            }
        }
        using namespace DirectX;
        Win32Window window;
        Require(window.Create(GetModuleHandleW(nullptr),SW_HIDE),"probe window");
        D3D11Renderer renderer;
        Require(renderer.Initialize(window.Handle(),window.Width(),window.Height()),"probe renderer");
        if(!model->clips.empty()) component.player.Play(0,true);
        RenderFrame frame; AppendModelDraws(component,XMMatrixIdentity(),frame);
        XMFLOAT3 low{FLT_MAX,FLT_MAX,FLT_MAX}, high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(const auto& item:frame.items) for(const auto& vertex:item.mesh->vertices)
        {
            auto p=XMLoadFloat3(&vertex.position);
            if(!item.bones.empty())
            {
                auto skinned=XMVectorZero();
                for(unsigned i=0;i<4;++i) if(vertex.weights[i]>0)
                    skinned+=XMVector3TransformCoord(p,XMLoadFloat4x4(&item.bones.at(vertex.bones[i])))*vertex.weights[i];
                p=skinned;
            }
            p=XMVector3TransformCoord(p,XMLoadFloat4x4(&item.world));
            XMFLOAT3 point; XMStoreFloat3(&point,p);
            Require(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"nonfinite skinned position");
            low.x=std::min(low.x,point.x);low.y=std::min(low.y,point.y);low.z=std::min(low.z,point.z);
            high.x=std::max(high.x,point.x);high.y=std::max(high.y,point.y);high.z=std::max(high.z,point.z);
        }
        const auto center=(XMLoadFloat3(&low)+XMLoadFloat3(&high))*.5f;
        const float radius=std::max(.01f,XMVectorGetX(XMVector3Length(XMLoadFloat3(&high)-XMLoadFloat3(&low)))*.5f);
        const auto eye=center+XMVectorSet(0,0,-radius*3,0);
        XMStoreFloat4x4(&frame.view,XMMatrixLookAtLH(eye,center,XMVectorSet(0,1,0,0)));
        XMStoreFloat4x4(&frame.projection,XMMatrixPerspectiveFovLH(XM_PIDIV4,float(window.Width())/window.Height(),radius*.01f,radius*10));
        std::uint64_t first=0,second=0;
        Require(renderer.RenderScene(frame,false,&first),"model GPU visibility");
        if(!model->clips.empty())
        {
            component.player.Advance(model->clips[0].duration*.37);
            frame.items.clear(); AppendModelDraws(component,XMMatrixIdentity(),frame);
            Require(renderer.RenderScene(frame,false,&second),"animated model GPU visibility");
            Log::Write(LogLevel::Info,std::string("GPU animation pixel change=")+(first!=second ? "yes" : "no (inspect static clip)"));
        }
        Log::Write(LogLevel::Info,"Imported bounds extent "+std::to_string(high.x-low.x)+", "+std::to_string(high.y-low.y)+", "+std::to_string(high.z-low.z));
        Log::Write(LogLevel::Info,"Model probe PASS "+path.string()); return 0;
    }
    catch(const std::exception& error) { Log::Write(LogLevel::Error,error.what()); return 1; }
}
}
