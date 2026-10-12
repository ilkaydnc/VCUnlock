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
//   FILE      perframe.cpp
//   MODULE    perframe
//   ROLE      Frame-rate fixes for code that runs once per drawn frame
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The HUD, the audio service, render-time effects (PreRender) and the frontend run once per drawn frame rather than
//   inside a 30 Hz world tick. Inside a world tick k = 1 and logicalPassed = 1, so these patches leave tick code
//   exactly as the game had it; on a drawn frame they scale by the number of ticks that frame ran (0 on most frames
//   above 30 fps).
//
//   Code that only runs inside world ticks needs nothing.
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
#include "fix.h"
#include "timer.h"
#include "modules.h"
#include <cstring>
#include <initializer_list>

namespace perframe {
using timer::Scale;

// ---------------------------------------------------------------------------------------------------------------------
//  GATE THUNKS
// ---------------------------------------------------------------------------------------------------------------------

// A tiny x86 emitter. B: raw bytes, Hex: a byte string, CmpLP: cmp dword [logicalPassed], 0,
// Jcc / Jmp: rel32 branches, Jcc8 + Bind: a forward rel8 branch.
enum : uint8_t { JE = 4, JNE = 5, JS = 8, JNS = 9, JGE = 0xD };
struct Emit {
    uint8_t* p;
    void B(std::initializer_list<uint8_t> l) { for (uint8_t b : l) *p++ = b; }
    void Hex(const char* s) {
        while (*s) {
            while (*s == ' ') ++s;
            if (!*s) break;
            *p++ = (uint8_t)strtoul(s, nullptr, 16);
            while (*s && *s != ' ') ++s;
        }
    }
    void U32(uint32_t v) { memcpy(p, &v, 4); p += 4; }
    void CmpLP() { B({0x83, 0x3D}); U32((uint32_t)(uintptr_t)&timer::logicalPassed); B({0x00}); }
    void Jcc(uint8_t cc, uintptr_t t) { B({0x0F, uint8_t(0x80 | cc)}); U32((uint32_t)(t - ((uintptr_t)p + 4))); }
    void Jmp(uintptr_t t) { B({0xE9}); U32((uint32_t)(t - ((uintptr_t)p + 4))); }
    uint8_t* Jcc8(uint8_t cc) { B({uint8_t(0x70 | cc), 0}); return p - 1; }
    void Bind(uint8_t* rel8) { *rel8 = (uint8_t)(p - (rel8 + 1)); }
};

using Builder = void (*)(Emit&);

static uint8_t* Build(Builder b) {
    uint8_t* t = fix::AllocCode(96);
    if (!t) return nullptr;
    Emit e{t};
    b(e);
    FlushInstructionCache(GetCurrentProcess(), t, e.p - t);
    return t;
}

// Replaces `len` bytes at `at` with a jmp to a generated thunk (the thunk jumps back itself).
static void Thunk(uintptr_t at, size_t len, const char* pat, const char* what, Builder b) {
    if (!mem::Match(at, pat)) {
        Log("fix [%s] byte check FAILED at %08X: want %s", what, (unsigned)at, pat);
        fix::Count(false);
        return;
    }
    uint8_t* t = Build(b);
    if (!t) {
        fix::Count(false);
        return;
    }
    mem::Jmp(at, t, len);
    fix::Count(true);
}

// Calls `target` only on frames with a world tick (void callee, result unused; `argBytes` popped by the callee).
static void GateCall(uintptr_t at, uintptr_t target, const char* what, uint16_t argBytes = 0) {
    static uintptr_t s_target;  // read by the builder below (single-threaded install)
    static uint16_t s_args;
    s_target = target;
    s_args = argBytes;
    uint8_t* t = Build([](Emit& e) {
        e.CmpLP();
        uint8_t* r = e.Jcc8(JE);
        e.Jmp(s_target);
        e.Bind(r);
        if (s_args) e.B({0xC2, uint8_t(s_args), uint8_t(s_args >> 8)});  // ret imm16
        else e.B({0xC3});
    });
    if (t) fix::RetargetCall(at, target, t, what);
}

// A float constant scaled for the current frame (see fix::Scaled).
static void S(uintptr_t insn, const char* pat, uintptr_t constAddr, Scale m, float expect, const char* what) {
    fix::Scaled(insn, pat, constAddr, m, what, expect);
}

// ---------------------------------------------------------------------------------------------------------------------
//  FRAMECOUNTER PREDICATES
// ---------------------------------------------------------------------------------------------------------------------

// FrameCounter only advances in world ticks, so a drawn-frame "(fc & m) == k" would hold on every frame until
// the next tick.
static uint32_t s_fcEdge0;  // FrameCounter on frames with a tick, else with its low byte cleared

static void UpdateCounters() {
    uint32_t fc = game::FrameCounter;
    s_fcEdge0 = timer::logicalPassed ? fc : fc & ~0xFFu;
}

// Hook-skip for per-object predicates "((seed + fc) & m) == k": frames without a tick take the no-event
// path, frames with one run the original instruction. `at` holds a 6-byte "op reg, [fc]".
static void Skip(uintptr_t at, const char* pat, uintptr_t noEvent, const char* what) {
    if (!mem::Match(at, pat)) {
        Log("fix [%s] byte check FAILED at %08X: want %s", what, (unsigned)at, pat);
        fix::Count(false);
        return;
    }
    uint8_t* t = fix::AllocCode(32);
    if (!t) {
        fix::Count(false);
        return;
    }
    uint8_t* p = t;
    auto u8 = [&](uint8_t v) { *p++ = v; };
    auto u32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };

    u8(0x83); u8(0x3D); u32((uint32_t)(uintptr_t)&timer::logicalPassed); u8(0x00);    // cmp dword [LP], 0
    u8(0x0F); u8(0x84); u32((uint32_t)(noEvent - ((uintptr_t)p + 4)));                // je noEvent
    memcpy(p, (const void*)at, 6); p += 6;                                            // op reg, [fc]
    u8(0xE9); u32((uint32_t)((at + 6) - ((uintptr_t)p + 4)));                         // jmp back

    FlushInstructionCache(GetCurrentProcess(), t, p - t);
    mem::Jmp(at, t, 6);
    fix::Count(true);
}

// ---------------------------------------------------------------------------------------------------------------------
//  HOOK HANDLERS
// ---------------------------------------------------------------------------------------------------------------------

// CDoor::Process (car doors in ticks; also called from the boat PreRender 0x5A53A0):
// angle += angVel*k (angVel stays in 30 Hz tick units).
static void __cdecl DoorAngle(fix::Regs* r, fix::Fpu*) {
    float* d = (float*)r->ebp;
    d[3] += d[5] * timer::k;
    r->cont = 0x5A9CC7;
}

// Water texture scroll (CWaterLevel::RenderWater): six offsets step once per drawn frame.
static float* const kWaterScroll = (float*)0x77EA6C;
static float s_waterScroll[6];
static bool s_waterScrolled;

static void __cdecl WaterScrollStart(fix::Regs*, fix::Fpu*) {
    memcpy(s_waterScroll, kWaterScroll, sizeof s_waterScroll);
    s_waterScrolled = true;
}

static void __cdecl WaterScrollEnd(fix::Regs*, fix::Fpu*) {
    if (!s_waterScrolled) return;
    s_waterScrolled = false;
    for (int i = 0; i < 6; ++i)
        kWaterScroll[i] = s_waterScroll[i] + (kWaterScroll[i] - s_waterScroll[i]) * timer::kFrame;
}

// ---------------------------------------------------------------------------------------------------------------------
//  HUD
// ---------------------------------------------------------------------------------------------------------------------

static void Hud() {
    // HUD per-frame counters
    Thunk(0x55936B, 6, "FF 0D 94 87 70 00", "hud media attention", [](Emit& e) {
        e.B({0x51});                                                            // push ecx
        e.B({0x8B, 0x0D}); e.U32((uint32_t)(uintptr_t)&timer::logicalPassed);   // mov ecx, [LP]
        e.Hex("29 0D 94 87 70 00");                                             // sub [0x708794], ecx
        uint8_t* ok = e.Jcc8(JNS);
        e.Hex("C7 05 94 87 70 00 00 00 00 00");                                 // mov dword [0x708794], 0
        e.Bind(ok);
        e.B({0x59});                                                            // pop ecx
        e.Jmp(0x559371);
    });
    Thunk(0x559F5A, 7, "66 FF 05 32 FA A0 00", "hud timer flash", [](Emit& e) {
        e.B({0x50});                                                            // push eax
        e.B({0xA1}); e.U32((uint32_t)(uintptr_t)&timer::logicalPassed);         // mov eax, [LP]
        e.Hex("66 01 05 32 FA A0 00");                                          // add word [0xA0FA32], ax
        e.B({0x58});                                                            // pop eax
        e.Jmp(0x559F61);
    });
    Thunk(0x55A1E5, 8, "66 FF 04 7D 38 F9 A0 00", "hud counter flash", [](Emit& e) {
        e.B({0x50});                                                            // push eax
        e.B({0xA1}); e.U32((uint32_t)(uintptr_t)&timer::logicalPassed);         // mov eax, [LP]
        e.Hex("66 01 04 7D 38 F9 A0 00");                                       // add word [edi*2+0xA0F938], ax
        e.B({0x58});                                                            // pop eax
        e.Jmp(0x55A1ED);
    });
    // Drunk blur colour cycle
    Thunk(0x55EB3A, 7, "80 3D 60 6D 69 00 FF", "drunk blur colour +", [](Emit& e) {
        e.CmpLP();
        e.Jcc(JE, 0x55EBC0);
        e.Hex("80 3D 60 6D 69 00 FF");
        e.Jmp(0x55EB41);
    });
    Thunk(0x55EB80, 7, "80 3D 60 6D 69 00 80", "drunk blur colour -", [](Emit& e) {
        e.CmpLP();
        e.Jcc(JE, 0x55EBC0);
        e.Hex("80 3D 60 6D 69 00 80");
        e.Jmp(0x55EB87);
    });
}

// ---------------------------------------------------------------------------------------------------------------------
//  AUDIO
// ---------------------------------------------------------------------------------------------------------------------

static void Audio() {
    // Releasing-sound fade
    Thunk(0x5F675E, 7, "8B 73 50 85 F6 74 67", "audio release fade", [](Emit& e) {
        e.Hex("8B 73 50 85 F6");  // mov esi,[ebx+0x50]; test esi,esi
        e.Jcc(JE, 0x5F67CC);
        e.CmpLP();
        e.Jcc(JE, 0x5F67B2);  // no fade/dec this frame, still requeue
        e.Jmp(0x5F6765);
    });
    // Audio manager frame counter
    Thunk(0x5F7783, 6, "FF 86 54 55 00 00", "audio frame counter", [](Emit& e) {
        e.CmpLP();
        e.Jcc(JE, 0x5F7789);
        e.Hex("FF 86 54 55 00 00");  // inc dword [esi+0x5554]
        e.Jmp(0x5F7789);
    });
    // Police radio, music manager and engine cruising pitch: step only on frames with a tick
    struct Step {
        uintptr_t at;
        size_t len;
        const char* bytes;
        uintptr_t next;
        const char* what;
    };
    static const Step kSteps[] = {
        {0x5FE361, 6, "FF 0D C8 29 78 00", 0x5FE39D, "police radio last seen"},
        {0x5FDF2A, 6, "FE 0D CD 29 78 00", 0x5FDF30, "police radio wait"},
        {0x5FDDC0, 8, "66 FF 84 13 E0 50 00 00", 0x5FDDC8, "crime age"},
        {0x5F9D80, 6, "FE 0D C5 29 78 00", 0x5F9D95, "radio station name"},
        {0x5F12BD, 6, "FF 0D D8 28 78 00", 0x5F12D7, "engine cruising down"},
        {0x5F12D1, 6, "FF 05 D8 28 78 00", 0x5F12D7, "engine cruising up"},
        {0x5FAA15, 6, "FE 83 98 39 00 00", 0x5FAA5A, "radio duck release 1"},
        {0x5FAA3B, 6, "FE 83 98 39 00 00", 0x5FAA5A, "radio duck release 2"},
    };
    for (const Step& s : kSteps) {
        static const Step* s_cur;
        s_cur = &s;
        Thunk(s.at, s.len, s.bytes, s.what, [](Emit& e) {
            e.CmpLP();
            uint8_t* skip = e.Jcc8(JE);
            e.Hex(s_cur->bytes);
            e.Bind(skip);
            e.Jmp(s_cur->next);
        });
    }
    Thunk(0x5FB7F0, 6, "FF 0D 9C 29 78 00", "radio retune counter", [](Emit& e) {
        e.Hex("83 3D 9C 29 78 00 13");  // cmp dword [0x78299C], 19
        uint8_t* dec = e.Jcc8(JGE);     // the "== 19" frame must last one frame
        e.CmpLP();
        uint8_t* skip = e.Jcc8(JE);
        e.Bind(dec);
        e.Hex("FF 0D 9C 29 78 00");  // dec dword [0x78299C]
        e.Bind(skip);
        e.Jmp(0x5FB7F6);
    });
    // Radio fade-in latency and +6 volume ramp (in game / frontend preview)
    Thunk(0x5FAA7D, 6, "80 7B 03 00 74 05", "radio fade in", [](Emit& e) {
        e.CmpLP();
        e.Jcc(JE, 0x5FAAA4);
        e.Hex("80 7B 03 00");  // cmp byte [ebx+3], 0
        e.Jcc(JE, 0x5FAA88);
        e.Jmp(0x5FAA83);
    });
    Thunk(0x5FBEA3, 6, "80 7B 03 00 74 07", "frontend radio fade in", [](Emit& e) {
        e.Hex("80 7B 03 00");
        uint8_t* lat = e.Jcc8(JNE);
        e.CmpLP();
        e.Jcc(JE, 0x5FBECC);  // set the unchanged volume
        e.Jmp(0x5FBEB0);
        e.Bind(lat);
        e.CmpLP();
        e.Jcc(JE, 0x5FBEE0);
        e.Jmp(0x5FBEA9);
    });
}

// ---------------------------------------------------------------------------------------------------------------------
//  WORLD RENDERING
// ---------------------------------------------------------------------------------------------------------------------

static void World() {
    // Rain streak 1/16 roll (render path): fail on frames without a tick
    if (uint8_t* t = Build([](Emit& e) {
            e.CmpLP();
            uint8_t* real = e.Jcc8(JNE);
            e.B({0xB8, 0x00, 0x0F, 0x00, 0x00, 0xC3});  // mov eax, 0xF00; ret
            e.Bind(real);
            e.Jmp(0x6489A0);  // rand
        }))
        fix::RetargetCall(0x57C0A0, 0x6489A0, t, "rain streak spawn");
    // Water scroll: scale this frame's step by the frame's length (the wrap checks follow at 0x5C17FF)
    fix::Hook(0x5C16D7, 6, "D9 05 6C EA 77 00", WaterScrollStart, "water scroll start");
    fix::Hook(0x5C17FF, 6, "D9 05 74 EA 77 00", WaterScrollEnd, "water scroll step");
    // CPed::PreRender: bike shirt flap (the wind sound is restarted every frame, keep it at 30 Hz), limb blood
    GateCall(0x4FE4EB, 0x5F9A00, "shirt flap sound", 12);
    fix::Redirect(0x4FEE03, "A1 A0 C8 A0 00 83 E0 07 83 F8 03", (uintptr_t)&game::FrameCounter, &s_fcEdge0,
                  "limb blood");
    // CAutomobile / CBike::PreRender damage particles and smoke
    Skip(0x591F05, "03 05 A0 C8 A0 00 83 E0 01 74 10", 0x591F10, "car damage particles");
    Skip(0x592277, "03 05 A0 C8 A0 00 83 E0 07", 0x5929A4, "car damage smoke 1");
    Skip(0x592422, "03 05 A0 C8 A0 00 83 E0 0F", 0x5929A8, "car damage smoke 2");
    Skip(0x592887, "03 05 A0 C8 A0 00 83 E0 0F", 0x5929B8, "car damage smoke 3");
    Skip(0x592934, "03 05 A0 C8 A0 00 83 E0 01", 0x5929C0, "car damage smoke 4");
    Skip(0x60D973, "03 05 A0 C8 A0 00 83 E0 01 74 07", 0x60D97E, "bike damage particles");
    Skip(0x60DDF5, "03 05 A0 C8 A0 00 83 E0 01 0F 85", 0x60DEC6, "bike damage smoke");
    // Rio fan (boat PreRender, vtable 0x69A0B4 slot 12)
    S(0x5A5E0E, "D9 05 B8 9E 69 00", 0x699EB8, Scale::Linear, 0.008f, "boat fan wind");
    S(0x5A5E1A, "D8 05 7C A0 69 00", 0x69A07C, Scale::Linear, 0.002f, "boat fan base");
    // Heli rotor angle (Render, vtable 0x69B21C slot 13)
    S(0x5AF056, "D8 05 44 B1 69 00", 0x69B144, Scale::Linear, 0.48307693f, "heli rotor angle");
    // Swinging doors / bonnets / boots
    S(0x5A9C83, "D8 0D 6C A3 69 00", 0x69A36C, Scale::Pow, 0.945f, "door swing damping");
    S(0x5A9C78, "D8 0D 68 A3 69 00", 0x69A368, Scale::Pow, 0.98f, "boat door swing damping");
    fix::Hook(0x5A9CBE, 9, "D9 45 0C D8 45 14 D9 5D 0C", DoorAngle, "door swing angle");
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void Install() {
    timer::OnFrame(UpdateCounters);
    UpdateCounters();
    int a0 = fix::Applied(), f0 = fix::Failed();
    Hud();
    Audio();
    World();
    Log("perframe: %d patches applied, %d failed", fix::Applied() - a0, fix::Failed() - f0);
}
}  // namespace perframe
