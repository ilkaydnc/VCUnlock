// =====================================================================================================================
//
//      _    __________  __      __           __
//     | |  / / ____/ / / /___  / /___  _____/ /__
//     | | / / /   / / / / __ \/ / __ \/ ___/ //_/        GRAND THEFT AUTO: VICE CITY
//     | |/ / /___/ /_/ / / / / / /_/ / /__/ ,<           Uncapped frame rate.
//     |___/\____/\____/_/ /_/_/\____/\___/_/|_|
//
// =====================================================================================================================
//
//   FILE      timer.cpp
//   MODULE    timer
//   ROLE      CTimer::Update replacement, frame pacing and the world clocks
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Drawn frames (Idle) only measure real time for the fixed world: the game clock, the time step and FrameCounter
//   advance once per world tick (FixedTick). Other callers (frontend, loading screens, cutscene loads) keep a variable
//   step with the pause, time-scale and clamp rules of the original CTimer::Update, with two differences:
//
//   - the millisecond clocks carry the fractional part (the original truncated it every frame);
//   - all math runs in double precision (the original ran in the 24-bit x87 mode D3D leaves behind).
//
//   Frame pacing waits on a high-resolution waitable timer and spins the last millisecond.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "core.h"
#include "game.h"
#include "timer.h"
#include "render.h"
#include "modules.h"
#include "fixed.h"
#include <intrin.h>
#include <cmath>

namespace timer {

// ---------------------------------------------------------------------------------------------------------------------
//  CLOCKS
// ---------------------------------------------------------------------------------------------------------------------

float k, kFrame;
uint32_t logicalPassed;

static double s_cyclesPerMs;
static double s_accMain, s_accPause, s_accLogical;
static ScaledConst s_pool[16];
static int s_poolN;

double Now() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return double(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / freq;
}

const float ts30 = (float)(50.0 / 30.0);

// 30 Hz logical frames in real time, like the original FrameCounter (ticks while paused too)
static uint32_t RealTimeLogical(double rawMs) {
    s_accLogical += rawMs;
    uint32_t lp = 0;
    while (s_accLogical >= 1000.0 / 30.0) {
        s_accLogical -= 1000.0 / 30.0;
        ++lp;
    }
    return lp > 4 ? 4 : lp;
}

// ---------------------------------------------------------------------------------------------------------------------
//  PER-FRAME SCALE FACTORS
// ---------------------------------------------------------------------------------------------------------------------

static void (*s_onFrame[16])();
static int s_onFrameN;
static void (*s_onDrawn[8])();
static int s_onDrawnN;

void OnFrame(void (*fn)()) {
    if (s_onFrameN < 16) s_onFrame[s_onFrameN++] = fn;
    else Log("OnFrame list full");
}

void OnDrawnFrame(void (*fn)()) {
    if (s_onDrawnN < 8) s_onDrawn[s_onDrawnN++] = fn;
    else Log("OnDrawnFrame list full");
}

ScaledConst* AddScaled(float base, Scale mode) {
    if (s_poolN >= (int)(sizeof s_pool / sizeof s_pool[0])) {
        Log("scaled const pool full");
        return nullptr;
    }
    ScaledConst* c = &s_pool[s_poolN++];
    c->base = base;
    c->mode = mode;
    c->value = base;
    return c;
}

static void RefreshFixes() {
    for (int i = 0; i < s_poolN; ++i) {
        ScaledConst& c = s_pool[i];
        c.value = (float)(c.mode == Scale::Pow ? pow((double)c.base, (double)k) : (double)c.base * k);
    }
    for (int i = 0; i < s_onFrameN; ++i) s_onFrame[i]();
}

// ---------------------------------------------------------------------------------------------------------------------
//  WORLD TICKS
// ---------------------------------------------------------------------------------------------------------------------

// One world tick: the game clocks advance by exactly 1/30 s (the ms clocks keep the fraction).
void FixedTick() {
    using namespace game;
    PrevTimeInMs() = TimeInMs();
    s_accMain += 1000.0 / 30.0;
    uint32_t elapsed = (uint32_t)(s_accMain + 1e-9);
    s_accMain -= elapsed;
    TimeInMs() += elapsed;
    TimeInMsNonClipped() += elapsed;
    TimeStep() = TimeStepNonClipped() = ts30;
    ++FrameCounter();
    logicalPassed = 1;
    k = 1.0f;
    RefreshFixes();
}

void FixedRender(uint32_t ticks) {
    // Code that runs once per drawn frame sees the time step of this frame's ticks (0 without a tick).
    game::TimeStep() = game::TimeStepNonClipped() = ts30 * ticks;
    k = (float)ticks;
    logicalPassed = ticks;
    RefreshFixes();
}

// ---------------------------------------------------------------------------------------------------------------------
//  FRAME PACING
// ---------------------------------------------------------------------------------------------------------------------

static HANDLE s_waitTimer;
static double s_lastFrame;
static int s_refreshHz;  // set by render module

void SetRefresh(int hz) { s_refreshHz = hz; }

static void WaitUntil(double t) {
    double now = Now();
    double remain = t - now;
    if (remain > 0.0015 && s_waitTimer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((remain - 0.001) * 1e7);
        if (SetWaitableTimerEx(s_waitTimer, &due, 0, nullptr, nullptr, nullptr, 0))
            WaitForSingleObject(s_waitTimer, 100);
    }
    while (Now() < t) _mm_pause();
}

static double FrameCap() {
    int cap = g_cfg.fpsLimit;
    if (g_cfg.vsync && s_refreshHz > 0 && cap >= s_refreshHz) cap = 0;  // vsync already limits
    // "unlimited" stops at the engine's own floor (ms_fTimeStep >= 0.01 = 0.2 ms = 5000 fps)
    return cap > 0 ? cap : 5000.0;
}

static void Pace() {
    double cap = FrameCap();
    int bg = window::BackgroundFps();
    if (bg > 0 && (cap <= 0 || cap > bg)) cap = bg;
    if (cap <= 0) return;
    double period = 1.0 / cap;
    double target = s_lastFrame + period;
    double now = Now();
    if (now < target) {
        WaitUntil(target);
        s_lastFrame = target;
    } else s_lastFrame = (now - target > period) ? now : target;
}

// ---------------------------------------------------------------------------------------------------------------------
//  STATS
// ---------------------------------------------------------------------------------------------------------------------

static double s_statT0;
static uint32_t s_statFrames, s_statGame0;
static double s_statMin = 1e9, s_statMax;
static double s_statTs;

static void Stats(double rawMs) {
    if (!g_cfg.log) return;
    double now = Now();
    if (s_statT0 == 0) {
        s_statT0 = now;
        s_statGame0 = game::TimeInMs();
        return;
    }
    ++s_statFrames;
    if (rawMs < s_statMin) s_statMin = rawMs;
    if (rawMs > s_statMax) s_statMax = rawMs;
    s_statTs += game::TimeStep();
    if (now - s_statT0 >= 5.0) {
        double sec = now - s_statT0;
        uint32_t gameMs = game::TimeInMs() - s_statGame0;
        Log("stats: %.1f fps  frame min %.2f max %.2f ms  avg ts %.4f  game clock %.4fx real  state %d menu %d  "
            "presents %.1f/s avg %.3f ms  interval %08X",
            s_statFrames / sec, s_statMin, s_statMax, s_statFrames ? s_statTs / s_statFrames : 0.0,
            gameMs / (sec * 1000.0), game::GameState(), game::MenuActive(), render::g_presents / sec,
            render::g_presents ? render::g_presentTime * 1000.0 / render::g_presents : 0.0, *(uint32_t*)At(0xA0ED3C));
        render::g_presents = 0;
        render::g_presentTime = 0;
        s_statT0 = now;
        s_statFrames = 0;
        s_statMin = 1e9;
        s_statMax = 0;
        s_statTs = 0;
        s_statGame0 = game::TimeInMs();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  CTIMER::UPDATE
// ---------------------------------------------------------------------------------------------------------------------

static void __cdecl Update() {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    if (ret == At(game::Idle_CallTimerUpdate_Ret) || ret == At(game::FrontendIdle_CallTimerUpdate_Ret)) {
        Pace();
        window::Tick();
    }

    using namespace game;
    PrevTimeInMs() = TimeInMs();

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // honour _oldPerfCounter adjustments made by CTimer::Resume/Initialise (low part only, like the original)
    uint32_t cycles = (now.LowPart - OldPerfCounterLo()) & 0x7FFFFFFF;
    OldPerfCounterLo() = now.LowPart;
    OldPerfCounterHi() = (uint32_t)now.HighPart;

    bool paused = UserPause() || CodePause();
    double rawMs = cycles / s_cyclesPerMs;
    double ft = paused ? rawMs : rawMs * TimeScale();

    // pause-mode clock (advances while paused too)
    s_accPause += ft;
    uint32_t dp = (uint32_t)s_accPause;
    s_accPause -= dp;
    TimeInMsPauseMode() += dp;

    if (ret == At(game::Idle_CallTimerUpdate_Ret)) {
        // Drawn frame: the fixed world takes the real time; the game clock advances in FixedTick only.
        bool stopped = paused || MenuActive() || MenuGameNotLoaded() || !Playing();
        fixed::FrameDelta(paused ? 0.0 : ft);
        kFrame = stopped ? 0.0f : (float)(ft / (1000.0 / 30.0));
        for (int i = 0; i < s_onDrawnN; ++i) s_onDrawn[i]();
        FixedRender(0);
        if (stopped) {
            // No world ticks, but the menu and the audio service (radio fade-out on Esc) keep counting.
            uint32_t lp = RealTimeLogical(rawMs);
            if (lp) {
                logicalPassed = lp;
                RefreshFixes();
            }
        }
        Stats(rawMs);
        return;
    }

    if (paused) {
        TimeStep() = 0.0f;
    } else {
        s_accMain += ft;
        uint32_t d = (uint32_t)s_accMain;
        s_accMain -= d;
        TimeInMs() += d;
        TimeInMsNonClipped() += d;
        TimeStep() = (float)(ft * 0.05);
    }
    if (TimeStep() < 0.01f && !paused && !SnapShotActive()) TimeStep() = 0.01f;
    TimeStepNonClipped() = TimeStep();
    if (RecordForGameState() != 2) {  // not playing back a recording
        if (TimeStep() > 3.0f) TimeStep() = 3.0f;
        if (TimeInMs() - PrevTimeInMs() > 60) {
            TimeInMs() = PrevTimeInMs() + 60;
            s_accMain = 0;
        }
    }
    if (RecordForChaseStatus() == 1) {
        TimeStep() = 1.0f;
        TimeInMs() = PrevTimeInMs() + 16;
    }
    ++FrameCounter();

    k = paused ? 0.0f : TimeStep() * 0.6f;
    kFrame = k;
    logicalPassed = RealTimeLogical(rawMs);
    RefreshFixes();
    Stats(rawMs);
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

bool Verify() {
    PatchGroup g("timer: CTimer::Update and the WinMain limiter");
    g.Expect(At(game::CTimer_Update), "55 A1 [973B34] 89 E5 A3 [977D94] A1 [690018]")
        .Expect(At(game::WinMain_LimiterCheck), "80 3D [86865D] 00 74 1C DB 05 [9B38F4] D8 3D [6D4E9C]");
    return g.Ok();
}

void Install() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_cyclesPerMs = double(f.QuadPart) / 1000.0;
    s_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!s_waitTimer) s_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    s_lastFrame = Now();
    mem::Jmp(At(game::CTimer_Update), (void*)&Update);
    // Old limiter in WinMain: always run the frame (pacing happens in Update). The distance is inside the checked
    // code (its jz reaches WinMain_RunFrame too), so it is the same in every version.
    constexpr uint8_t kToRunFrame = uint8_t(game::WinMain_RunFrame - (game::WinMain_LimiterCheck + 2));
    uint8_t jmp[2] = {0xEB, kToRunFrame};
    mem::Write(At(game::WinMain_LimiterCheck), jmp, 2);
}
}  // namespace timer
