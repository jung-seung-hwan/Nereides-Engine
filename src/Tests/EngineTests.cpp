#include "Tests/EngineTests.h"
#include "Core/Input.h"
#include "Core/Time.h"
#include "Core/Log.h"
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
}
int RunEngineTests()
{
    try { TestInput(); TestTime(); Log::Write(LogLevel::Info, "Engine tests PASS: input, time"); return 0; }
    catch (const std::exception& error) { Log::Write(LogLevel::Error, error.what()); return 1; }
}
}
