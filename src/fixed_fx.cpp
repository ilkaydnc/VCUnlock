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
//   FILE      fixed_fx.cpp
//   MODULE    fixed
//   ROLE      World effects drawn from the fixed 30 Hz world
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Effect state written by the world tick is captured after every tick and drawn between the last two ticks, like the
//   entities. Effects that are a function of the clock read the presentation clock instead. Everything shifted for a
//   drawn frame is put back before the next tick.
//
//   Covered: markers, bullet traces and money, rubbish, glass, water cannons, ropes, plane banners and trails, coronas,
//   the sun, moving buildings, skidmarks, antennas, rain and trees.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "fixed_fx.h"
#include "core.h"
#include "game.h"
#include "fix.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fixed {
namespace fx {
namespace {
using namespace fixedmath;

// ---------------------------------------------------------------------------------------------------------------------
//  HELPERS AND STATE
// ---------------------------------------------------------------------------------------------------------------------

template <class T> T& Field(void* p, size_t offset) { return *(T*)((uint8_t*)p + offset); }
uint8_t* At(uintptr_t base, int i, size_t stride) { return (uint8_t*)base + i * stride; }
float Dist2(Vec a, Vec b) {
    Vec d = a - b;
    return Dot(d, d);
}

bool Same(const void* a, const void* b, size_t n) { return !memcmp(a, b, n); }

const uint32_t* s_clock = nullptr;  // presentation clock: TimeInMs in ticks, interpolated while drawing
bool s_installed = false, s_drawing = false, s_ticking = false;
float s_alpha = 0;
uint32_t s_frameTicks = 0, s_tickIndex = 0, s_lastDrawClock = 0;
bool s_lastDrawValid = false;
Vec s_tickCamera{};
float s_markerStep = 0;  // TimeStep for the 3D marker spin; the presentation clock's step while drawing
bool s_markersOk = false, s_timesOk = false, s_rubbishOk = false, s_glassOk = false, s_cannonsOk = false,
     s_ropesOk = false, s_fluffOk = false, s_coronasOk = false, s_sunOk = false, s_movingOk = false;

// ---------------------------------------------------------------------------------------------------------------------
//  3D MARKERS
// ---------------------------------------------------------------------------------------------------------------------

// C3dMarkers (32 x 0x78 at 0x813020: atomic +0x48, used +0x52, identifier +0x54). Render clears `used` on every
// drawn frame and deletes markers left unused, so a marker placed by a tick (mission locates, script spheres)
// would only be drawn on the drawn frame right after it. Tick-placed markers stay until the next tick.
constexpr uintptr_t kMarkers = 0x813020;
constexpr int kNumMarkers = 32;
constexpr size_t kMarkerStride = 0x78;
struct MarkerTrack {
    bool tick = false, pre = false;
    uint32_t id = 0;
};

MarkerTrack s_markers[kNumMarkers];

void MarkersBeforeTick() {
    for (int i = 0; i < kNumMarkers; i++) {
        uint8_t* m = At(kMarkers, i, kMarkerStride);
        MarkerTrack& t = s_markers[i];
        // As if a drawn frame had consumed it: the tick's PlaceMarker finds it by identifier again.
        if (t.tick && Field<uint32_t>(m, 0x54) == t.id) Field<uint8_t>(m, 0x52) = 0;
        t.tick = false;
        t.pre = Field<uint8_t>(m, 0x52) != 0;
    }
}

void MarkersAfterTick() {
    for (int i = 0; i < kNumMarkers; i++) {
        uint8_t* m = At(kMarkers, i, kMarkerStride);
        MarkerTrack& t = s_markers[i];
        t.tick = Field<uint8_t>(m, 0x52) && !t.pre;
        t.id = Field<uint32_t>(m, 0x54);
    }
}

void MarkersRearm() {
    for (int i = 0; i < kNumMarkers; i++) {
        uint8_t* m = At(kMarkers, i, kMarkerStride);
        MarkerTrack& t = s_markers[i];
        if (t.tick && Field<uint32_t>(m, 0x54) == t.id && Field<void*>(m, 0x48)) Field<uint8_t>(m, 0x52) = 1;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  BULLET TRACES AND MONEY MESSAGES
// ---------------------------------------------------------------------------------------------------------------------

// Bullet traces (16 x 0x2C at 0x8108D8: in use +0x18, created +0x1C) and money messages (16 x 0x3C at 0x80F170:
// registered +0, 0 = free) age by TimeInMs - created, unsigned. While drawing they are given the creation time
// that makes that difference max(0, presentation clock - created).
constexpr uintptr_t kTraces = 0x8108D8, kMoney = 0x80F170;
struct TimeShift {
    uint32_t* field;
    uint32_t original, shifted;
};

TimeShift s_times[32];
int s_numTimes = 0;

void ShiftTime(uint32_t* field, bool nonZero) {
    uint32_t now = game::TimeInMs, created = *field, lag = now - *s_clock;
    if (!lag || created > now) return;
    uint32_t shifted = (uint32_t)std::min<uint64_t>(now, (uint64_t)created + lag);
    if (shifted == created || (nonZero && !shifted)) return;
    s_times[s_numTimes++] = {field, created, shifted};
    *field = shifted;
}

void ShiftTimes() {
    s_numTimes = 0;
    if (!s_timesOk) return;
    for (int i = 0; i < 16; i++) {
        uint8_t* t = At(kTraces, i, 0x2C);
        if (Field<uint8_t>(t, 0x18)) ShiftTime(&Field<uint32_t>(t, 0x1C), false);
        uint8_t* m = At(kMoney, i, 0x3C);
        if (Field<uint32_t>(m, 0)) ShiftTime(&Field<uint32_t>(m, 0), true);
    }
}

void RestoreTimes() {
    // A message the renderer expired (zeroed) stays expired.
    for (int i = 0; i < s_numTimes; i++)
        if (*s_times[i].field == s_times[i].shifted) *s_times[i].field = s_times[i].original;
    s_numTimes = 0;
}

auto SpecialFxRender = (void(__cdecl*)())0x574680;
void __cdecl SpecialFxRenderHook() {
    bool active = s_drawing;
    if (active) {
        if (s_markersOk) MarkersRearm();
        ShiftTimes();
    }
    SpecialFxRender();
    if (active) RestoreTimes();
}

// ---------------------------------------------------------------------------------------------------------------------
//  TICK HISTORY
// ---------------------------------------------------------------------------------------------------------------------

// Snapshot history of one tick-moved effect: the last two ticks' state.
template <class T> struct History {
    T prev{}, cur{};
    bool prevValid = false, curValid = false;
    void Push(const T& now) {
        prev = cur;
        prevValid = curValid;
        cur = now;
        curValid = true;
    }
    bool Both() const { return prevValid && curValid; }
};

// ---------------------------------------------------------------------------------------------------------------------
//  RUBBISH
// ---------------------------------------------------------------------------------------------------------------------

// CRubbish (32 x 0x44 at 0x7F8300): base position +0, animated position +0xC, state +0x1C (0 free, 1 lying,
// 2 moving), move start +0x20, angle +0x34. Render draws a lying sheet at its base position, a moving one at
// its animated position.
constexpr uintptr_t kSheets = 0x7F8300;
constexpr int kNumSheets = 32;
constexpr size_t kSheetStride = 0x44;
struct Sheet {
    int8_t state = 0;
    uint32_t moveStart = 0;
    Vec pos{};
    float angle = 0;
};

struct SheetTrack {
    History<Sheet> h;
    bool shifted = false;
    size_t offset = 0;
    Vec backupPos{};
    float backupAngle = 0;
};

SheetTrack s_sheets[kNumSheets];

size_t SheetOffset(int8_t state) { return state == 2 ? 0xC : 0; }

Sheet ReadSheet(uint8_t* p) {
    Sheet s;
    s.state = Field<int8_t>(p, 0x1C);
    s.moveStart = Field<uint32_t>(p, 0x20);
    s.pos = Field<Vec>(p, SheetOffset(s.state));
    s.angle = Field<float>(p, 0x34);
    return s;
}

bool SameSheet(const Sheet& a, const Sheet& b) {
    return a.state == b.state && a.moveStart == b.moveStart && Same(&a.pos, &b.pos, sizeof a.pos) &&
           Same(&a.angle, &b.angle, 4);
}

void ShiftSheets() {
    for (int i = 0; i < kNumSheets; i++) {
        SheetTrack& t = s_sheets[i];
        uint8_t* p = At(kSheets, i, kSheetStride);
        if (!t.h.Both() || !t.h.prev.state || !t.h.cur.state || !SameSheet(ReadSheet(p), t.h.cur)) continue;
        const Sheet &a = t.h.prev, &b = t.h.cur;
        if (!Finite(a.pos) || !Finite(b.pos) || Dist2(a.pos, b.pos) > 25.0f || !std::isfinite(a.angle) ||
            !std::isfinite(b.angle))
            continue;
        float turn = WrapPi(b.angle - a.angle);
        if (Same(&a.pos, &b.pos, sizeof a.pos) && turn == 0) continue;
        t.offset = SheetOffset(b.state);
        t.backupPos = Field<Vec>(p, t.offset);
        t.backupAngle = Field<float>(p, 0x34);
        t.shifted = true;
        Field<Vec>(p, t.offset) = Lerp(a.pos, b.pos, s_alpha);
        if (turn != 0) Field<float>(p, 0x34) = a.angle + turn * s_alpha;
    }
}

void RestoreSheets() {
    for (int i = 0; i < kNumSheets; i++) {
        SheetTrack& t = s_sheets[i];
        uint8_t* p = At(kSheets, i, kSheetStride);
        if (t.shifted) {
            Field<Vec>(p, t.offset) = t.backupPos;
            Field<float>(p, 0x34) = t.backupAngle;
            t.shifted = false;
        }
    }
}

auto RubbishRender = (void(__cdecl*)())0x567E10;
void __cdecl RubbishRenderHook() {
    bool active = s_drawing && s_rubbishOk;
    if (active) ShiftSheets();
    RubbishRender();
    if (active) RestoreSheets();
}

// ---------------------------------------------------------------------------------------------------------------------
//  FALLING GLASS
// ---------------------------------------------------------------------------------------------------------------------

// Falling glass (45 x 0x70 at 0x7D8710): a matrix at +0 (rows right/up/at/pos), start time +0x60, active +0x6D.
// The renderer only transforms the pane's vertices by the matrix, so blending the rows blends the vertices.
constexpr uintptr_t kPanes = 0x7D8710;
constexpr int kNumPanes = 45;
constexpr size_t kPaneStride = 0x70;
struct Pane {
    uint8_t active = 0;
    uint32_t timer = 0;
    Matrix m{};
};

struct PaneTrack {
    History<Pane> h;
    bool shifted = false;
    Matrix backup{};
};

PaneTrack s_panes[kNumPanes];

Pane ReadPane(uint8_t* p) {
    Pane s;
    s.active = Field<uint8_t>(p, 0x6D);
    s.timer = Field<uint32_t>(p, 0x60);
    s.m = Field<Matrix>(p, 0);
    return s;
}

void ShiftPanes() {
    for (int i = 0; i < kNumPanes; i++) {
        PaneTrack& t = s_panes[i];
        uint8_t* p = At(kPanes, i, kPaneStride);
        if (!t.h.Both()) continue;
        const Pane &a = t.h.prev, &b = t.h.cur;
        Pane now = ReadPane(p);
        if (!a.active || !b.active || a.timer != b.timer || !now.active || now.timer != b.timer ||
            !Same(&now.m, &b.m, sizeof b.m))
            continue;
        if (!Valid(a.m) || !Valid(b.m) || Dist2(a.m.pos, b.m.pos) > 25.0f || Same(&a.m, &b.m, sizeof b.m)) continue;
        Matrix m = b.m;
        m.right = Lerp(a.m.right, b.m.right, s_alpha);
        m.forward = Lerp(a.m.forward, b.m.forward, s_alpha);
        m.up = Lerp(a.m.up, b.m.up, s_alpha);
        m.pos = Lerp(a.m.pos, b.m.pos, s_alpha);
        t.backup = b.m;
        t.shifted = true;
        Field<Matrix>(p, 0) = m;
    }
}

void RestorePanes() {
    for (int i = 0; i < kNumPanes; i++)
        if (s_panes[i].shifted) {
            Field<Matrix>(At(kPanes, i, kPaneStride), 0) = s_panes[i].backup;
            s_panes[i].shifted = false;
        }
}

auto GlassRender = (void(__cdecl*)())0x555CC0;
void __cdecl GlassRenderHook() {
    bool active = s_drawing && s_glassOk;
    if (active) ShiftPanes();
    GlassRender();
    if (active) RestorePanes();
}

// ---------------------------------------------------------------------------------------------------------------------
//  WATER CANNONS
// ---------------------------------------------------------------------------------------------------------------------

// Water cannons (3 x 0x19C at 0x97E354: id +0, newest point +4 (int16), points +0xC, used +0x18C). The world
// tick writes the nozzle into the newest point from the tick pose of the fire truck; when the jet moves on to
// a new point, that point starts where the previous nozzle was.
constexpr uintptr_t kCannons = 0x97E354;
constexpr int kNumCannons = 3;
constexpr size_t kCannonStride = 0x19C;
struct Cannon {
    uint32_t id = 0;
    int16_t cur = 0;
    Vec pos[16]{};
    uint8_t used[16]{};
};

struct CannonTrack {
    History<Cannon> h;
    bool shifted = false;
    Vec backup[16]{};
};

CannonTrack s_cannons[kNumCannons];

Cannon ReadCannon(uint8_t* p) {
    Cannon c;
    c.id = Field<uint32_t>(p, 0);
    c.cur = Field<int16_t>(p, 4);
    memcpy(c.pos, p + 0xC, sizeof c.pos);
    memcpy(c.used, p + 0x18C, sizeof c.used);
    return c;
}

bool SameCannon(const Cannon& a, const Cannon& b) {
    return a.id == b.id && a.cur == b.cur && Same(a.pos, b.pos, sizeof a.pos) && Same(a.used, b.used, sizeof a.used);
}

void ShiftCannons() {
    for (int i = 0; i < kNumCannons; i++) {
        CannonTrack& t = s_cannons[i];
        uint8_t* p = At(kCannons, i, kCannonStride);
        if (!t.h.Both()) continue;
        const Cannon &a = t.h.prev, &b = t.h.cur;
        if (!b.id || a.id != b.id || a.cur < 0 || a.cur > 15 || b.cur < 0 || b.cur > 15 ||
            !SameCannon(ReadCannon(p), b))
            continue;
        Vec drawn[16];
        memcpy(drawn, b.pos, sizeof drawn);
        bool moved = false;
        for (int k = 0; k < 16; k++) {
            if (!b.used[k]) continue;
            Vec from;
            if (k == b.cur && a.cur != b.cur) {  // the jet's new point
                if (!a.used[a.cur]) continue;
                from = a.pos[a.cur];
            } else if (a.used[k]) from = a.pos[k];
            else continue;
            if (!Finite(from) || !Finite(b.pos[k]) || Dist2(from, b.pos[k]) > 100.0f) continue;
            drawn[k] = Lerp(from, b.pos[k], s_alpha);
            moved = true;
        }
        if (!moved) continue;
        memcpy(t.backup, b.pos, sizeof t.backup);
        t.shifted = true;
        memcpy(p + 0xC, drawn, sizeof drawn);
    }
}

void RestoreCannons() {
    for (int i = 0; i < kNumCannons; i++)
        if (s_cannons[i].shifted) {
            memcpy(At(kCannons, i, kCannonStride) + 0xC, s_cannons[i].backup, sizeof s_cannons[i].backup);
            s_cannons[i].shifted = false;
        }
}

// The jet's texture offset is a rand() per drawn frame; it is rolled on drawn frames with a tick only.
auto Rand = (int(__cdecl*)())0x6489A0;
int s_cannonRand[kNumCannons];
int s_cannonRands = 0, s_cannonCalls = 0;
bool s_cannonRoll = true;

int __cdecl CannonRandHook() {
    if (!s_drawing) return Rand();
    int i = s_cannonCalls++;
    if (!s_cannonRoll && i < s_cannonRands) return s_cannonRand[i];
    int v = Rand();
    if (i < kNumCannons) {
        s_cannonRand[i] = v;
        s_cannonRands = std::max(s_cannonRands, i + 1);
    }
    return v;
}

auto CannonsRender = (void(__cdecl*)())0x57AFD0;
void __cdecl CannonsRenderHook() {
    bool active = s_drawing && s_cannonsOk;
    if (active) ShiftCannons();
    CannonsRender();
    if (active) RestoreCannons();
}

// ---------------------------------------------------------------------------------------------------------------------
//  ROPES
// ---------------------------------------------------------------------------------------------------------------------

// Ropes (8 x 0x30C at 0x935920: active +0, id +4, points +0xC): the top hangs from the tick pose of the heli.
constexpr uintptr_t kRopes = 0x935920;
constexpr int kNumRopes = 8;
constexpr size_t kRopeStride = 0x30C;
struct Rope {
    uint8_t active = 0;
    uint32_t id = 0;
    Vec pos[32]{};
};

struct RopeTrack {
    History<Rope> h;
    bool shifted = false;
    Vec backup[32]{};
};

RopeTrack s_ropes[kNumRopes];

Rope ReadRope(uint8_t* p) {
    Rope r;
    r.active = Field<uint8_t>(p, 0);
    r.id = Field<uint32_t>(p, 4);
    memcpy(r.pos, p + 0xC, sizeof r.pos);
    return r;
}

// Blends a run of points between the last two ticks when every point moved less than `limit`.
bool BlendPoints(const Vec* a, const Vec* b, Vec* out, int n, float limit) {
    bool moved = false;
    for (int k = 0; k < n; k++) {
        if (!Finite(a[k]) || !Finite(b[k]) || Dist2(a[k], b[k]) > limit * limit) return false;
        moved |= !Same(&a[k], &b[k], sizeof(Vec));
    }
    if (!moved) return false;
    for (int k = 0; k < n; k++) out[k] = Lerp(a[k], b[k], s_alpha);
    return true;
}

void ShiftRopes() {
    for (int i = 0; i < kNumRopes; i++) {
        RopeTrack& t = s_ropes[i];
        uint8_t* p = At(kRopes, i, kRopeStride);
        if (!t.h.Both()) continue;
        const Rope &a = t.h.prev, &b = t.h.cur;
        Rope now = ReadRope(p);
        if (!a.active || !b.active || a.id != b.id || !now.active || now.id != b.id ||
            !Same(now.pos, b.pos, sizeof b.pos))
            continue;
        Vec drawn[32];
        if (!BlendPoints(a.pos, b.pos, drawn, 32, 20.0f)) continue;
        memcpy(t.backup, b.pos, sizeof t.backup);
        t.shifted = true;
        memcpy(p + 0xC, drawn, sizeof drawn);
    }
}

void RestoreRopes() {
    for (int i = 0; i < kNumRopes; i++)
        if (s_ropes[i].shifted) {
            memcpy(At(kRopes, i, kRopeStride) + 0xC, s_ropes[i].backup, sizeof s_ropes[i].backup);
            s_ropes[i].shifted = false;
        }
}

auto RopesRender = (void(__cdecl*)())0x632C80;
void __cdecl RopesRenderHook() {
    bool active = s_drawing && s_ropesOk;
    if (active) ShiftRopes();
    RopesRender();
    if (active) RestoreRopes();
}

// ---------------------------------------------------------------------------------------------------------------------
//  PLANE BANNERS AND TRAILS
// ---------------------------------------------------------------------------------------------------------------------

// Plane banners (5 x 8 points at 0x9333C8, parked below z -50) and plane trail heads (6 x 0x100 at 0x9335A8,
// point 0 = the plane): the planes are drawn a tick behind, the points are at the tick.
constexpr uintptr_t kBanners = 0x9333C8, kTrails = 0x9335A8;
constexpr int kNumBanners = 5, kNumTrails = 6;
struct Fluff {
    Vec banner[kNumBanners][8];
    Vec trail[kNumTrails];
};

History<Fluff> s_fluff;
Fluff s_fluffBackup;
bool s_bannerShifted[kNumBanners], s_trailShifted[kNumTrails];

Fluff ReadFluff() {
    Fluff f;
    memcpy(f.banner, (void*)kBanners, sizeof f.banner);
    for (int i = 0; i < kNumTrails; i++) f.trail[i] = Field<Vec>(At(kTrails, i, 0x100), 0);
    return f;
}

void ShiftFluff() {
    if (!s_fluff.Both()) return;
    const Fluff &a = s_fluff.prev, &b = s_fluff.cur;
    Fluff now = ReadFluff();
    for (int i = 0; i < kNumBanners; i++) {
        Vec* p = (Vec*)At(kBanners, i, 0x60);
        if (a.banner[i][0].z <= -50 || b.banner[i][0].z <= -50 || !Same(now.banner[i], b.banner[i], sizeof b.banner[i]))
            continue;
        Vec drawn[8];
        if (!BlendPoints(a.banner[i], b.banner[i], drawn, 8, 20.0f)) continue;
        memcpy(s_fluffBackup.banner[i], b.banner[i], sizeof drawn);
        memcpy(p, drawn, sizeof drawn);
        s_bannerShifted[i] = true;
    }
    for (int i = 0; i < kNumTrails; i++) {
        Vec drawn;
        if (!Same(&now.trail[i], &b.trail[i], sizeof(Vec)) || !BlendPoints(&a.trail[i], &b.trail[i], &drawn, 1, 30.0f))
            continue;
        s_fluffBackup.trail[i] = b.trail[i];
        Field<Vec>(At(kTrails, i, 0x100), 0) = drawn;
        s_trailShifted[i] = true;
    }
}

void RestoreFluff() {
    for (int i = 0; i < kNumBanners; i++)
        if (s_bannerShifted[i]) {
            memcpy(At(kBanners, i, 0x60), s_fluffBackup.banner[i], sizeof s_fluffBackup.banner[i]);
            s_bannerShifted[i] = false;
        }
    for (int i = 0; i < kNumTrails; i++)
        if (s_trailShifted[i]) {
            Field<Vec>(At(kTrails, i, 0x100), 0) = s_fluffBackup.trail[i];
            s_trailShifted[i] = false;
        }
}

auto MovingThingsRender = (void(__cdecl*)())0x54F140;
void __cdecl MovingThingsRenderHook() {
    bool active = s_drawing && s_fluffOk;
    if (active) ShiftFluff();
    MovingThingsRender();
    if (active) RestoreFluff();
}

// ---------------------------------------------------------------------------------------------------------------------
//  CORONA LIGHT STREAKS
// ---------------------------------------------------------------------------------------------------------------------

// Coronas (56 x 0x68 at 0x814130): Render moves each corona's screen history (x +0x36, y +0x42 int16[6];
// red/green/blue +0x4E/+0x54/+0x5A, valid +0x60 uint8[6]) along by one on every drawn frame, and light
// streaks are drawn through it. On drawn frames without a tick the history is moved back first, so the
// streak keeps one sample per tick, as at 30 fps.
constexpr uintptr_t kCoronas = 0x814130;
constexpr int kNumCoronas = 56;
constexpr size_t kCoronaStride = 0x68;

void HoldCoronaHistory() {
    for (int i = 0; i < kNumCoronas; i++) {
        uint8_t* c = At(kCoronas, i, kCoronaStride);
        memmove(c + 0x36, c + 0x38, 10);
        memmove(c + 0x42, c + 0x44, 10);
        for (size_t off : {0x4E, 0x54, 0x5A, 0x60}) memmove(c + off, c + off + 1, 5);
    }
}

auto CoronasRender = (void(__cdecl*)())0x5433F0;
void __cdecl CoronasRenderHook() {
    if (s_drawing && s_coronasOk && !s_frameTicks) HoldCoronaHistory();
    CoronasRender();
}

// ---------------------------------------------------------------------------------------------------------------------
//  THE SUN
// ---------------------------------------------------------------------------------------------------------------------

// The world tick puts it 150 m from the tick camera (sun direction table 0x791C78 by time-cycle slot
// 0xA0C000, coronas 1 and 2) and stores its screen position (0x9B38D4/0x9B38D0) for the clouds.
constexpr uintptr_t kSunDirections = 0x791C78, kTimeCycleSlot = 0xA0C000, kSunDistance = 0x6948A0;
constexpr uintptr_t kSunScreenX = 0x9B38D4, kSunScreenY = 0x9B38D0, kCameraPos = 0x7E36C0;
auto CalcScreenCoors = (bool(__cdecl*)(const Vec*, Vec*, float*, float*, bool))0x5777A0;
struct SunShift {
    Vec* coors;
    Vec original, shifted;
};

SunShift s_sunShift[2];
int s_numSunShift = 0;
float s_sunScreen[2];
bool s_sunScreenSaved = false;

bool SunAt(Vec camera, Vec& sun) {
    int slot = *(int32_t*)kTimeCycleSlot;
    if (slot < 0 || slot >= 64) return false;
    Vec d = *(Vec*)(kSunDirections + 12 * slot);
    float k = *(float*)kSunDistance;
    float x = d.x * k, y = d.y * k, z = d.z * k;  // the game's rounding order
    sun = {x + camera.x, y + camera.y, z + camera.z};
    return Finite(sun);
}

void PresentSun(Vec camera) {
    Vec tickSun, drawSun;
    if (!SunAt(s_tickCamera, tickSun) || !SunAt(camera, drawSun) || Same(&tickSun, &drawSun, sizeof drawSun)) return;
    for (int i = 0; i < kNumCoronas && s_numSunShift < 2; i++) {
        uint8_t* c = At(kCoronas, i, kCoronaStride);
        uint32_t id = Field<uint32_t>(c, 0xC);
        Vec& coors = Field<Vec>(c, 0);
        if ((id == 1 || id == 2) && Dist2(coors, tickSun) < 1e-4f) {
            s_sunShift[s_numSunShift++] = {&coors, coors, drawSun};
            coors = drawSun;
        }
    }
    s_sunScreen[0] = *(float*)kSunScreenX;
    s_sunScreen[1] = *(float*)kSunScreenY;
    s_sunScreenSaved = true;
    Vec screen;
    float w, h;
    if (CalcScreenCoors(&drawSun, &screen, &w, &h, true)) {
        *(float*)kSunScreenX = screen.x;
        *(float*)kSunScreenY = screen.y;
    } else *(float*)kSunScreenX = *(float*)kSunScreenY = 1000000.0f;
}

void RestoreSun() {
    for (int i = 0; i < s_numSunShift; i++)
        if (Same(s_sunShift[i].coors, &s_sunShift[i].shifted, sizeof(Vec)))
            *s_sunShift[i].coors = s_sunShift[i].original;
    s_numSunShift = 0;
    if (s_sunScreenSaved) {
        *(float*)kSunScreenX = s_sunScreen[0];
        *(float*)kSunScreenY = s_sunScreen[1];
        s_sunScreenSaved = false;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  MOVING BUILDINGS
// ---------------------------------------------------------------------------------------------------------------------

// Moving buildings (48 x 0x1C at 0x7D2860, count 0xA0F944: far away +0xA, building +0x18): the lighthouse beam,
// airport radar, fans and blimp are posed from the clock by CMovingThing::Update, which reads the presentation
// clock. Close ones are posed again for the drawn frame (with the tick camera, so the close/far lists stay as
// the tick left them) and back at the tick time afterwards.
constexpr uintptr_t kMovingThings = 0x7D2860, kNumMovingThings = 0xA0F944;
constexpr int kMaxMovingThings = 48;
constexpr size_t kMovingStride = 0x1C;
auto MovingThingUpdate = (void(__fastcall*)(void*, void*))0x54EB40;
uint8_t s_reposed[kMaxMovingThings];
int s_numReposed = 0;

void ReposeBuildings() {
    s_numReposed = 0;
    if (*s_clock == game::TimeInMs) return;
    int n = std::min<int>(*(uint16_t*)kNumMovingThings, kMaxMovingThings);
    for (int i = 0; i < n; i++) {
        uint8_t* t = At(kMovingThings, i, kMovingStride);
        if (Field<uint16_t>(t, 0xA) || !Field<void*>(t, 0x18)) continue;
        MovingThingUpdate(t, nullptr);
        s_reposed[s_numReposed++] = (uint8_t)i;
    }
}

void RestoreBuildings() {
    // Runs with the clock back at TimeInMs, which is when the world tick posed them.
    for (int k = 0; k < s_numReposed; k++) {
        uint8_t* t = At(kMovingThings, s_reposed[k], kMovingStride);
        if (Field<void*>(t, 0x18)) MovingThingUpdate(t, nullptr);
    }
    s_numReposed = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
//  SKIDMARKS AND ANTENNAS
// ---------------------------------------------------------------------------------------------------------------------

// CSkidmarks::Update ends every mark PreRender did not continue since the last update, and CAntennas::Update
// frees antennas not registered since then; both registrations happen once per drawn frame. With several ticks
// per drawn frame (below 30 fps) only the first tick updates them.
auto SkidmarksUpdate = (void(__cdecl*)())0x56FDF0, AntennasUpdate = (void(__cdecl*)())0x53EF70;
void __cdecl SkidmarksUpdateHook() {
    if (!s_ticking || !s_tickIndex) SkidmarksUpdate();
}

void __cdecl AntennasUpdateHook() {
    if (!s_ticking || !s_tickIndex) AntennasUpdate();
}

// ---------------------------------------------------------------------------------------------------------------------
//  CAPTURE
// ---------------------------------------------------------------------------------------------------------------------

// After every tick: the state the next drawn frames blend towards.
void CaptureAll() {
    if (s_rubbishOk)
        for (int i = 0; i < kNumSheets; i++) s_sheets[i].h.Push(ReadSheet(At(kSheets, i, kSheetStride)));
    if (s_glassOk)
        for (int i = 0; i < kNumPanes; i++) s_panes[i].h.Push(ReadPane(At(kPanes, i, kPaneStride)));
    if (s_cannonsOk)
        for (int i = 0; i < kNumCannons; i++) s_cannons[i].h.Push(ReadCannon(At(kCannons, i, kCannonStride)));
    if (s_ropesOk)
        for (int i = 0; i < kNumRopes; i++) s_ropes[i].h.Push(ReadRope(At(kRopes, i, kRopeStride)));
    if (s_fluffOk) s_fluff.Push(ReadFluff());
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void Install(const uint32_t* presentationClock) {
    s_clock = presentationClock;

    PatchGroup m("fixed: 3D marker layout");
    m.Expect(0x5746BC, "E8 CF C3 FF FF")                             // CSpecialFX::Render -> C3dMarkers::Render
        .Expect(0x570AA4, "BD 20 30 81 00")                          //   32 markers at 0x813020
        .Expect(0x570AB0, "80 7D 52 00")                             //   used +0x52
        .Expect(0x570B35, "C6 45 52 00")                             //   drawn: used = 0
        // unused with an atomic (+0x48): deleted
        .Expect(0x570B41, "83 7D 48 00 74 37 C7 45 54 00 00 00 00 C7 45 60 00 00 00 00")
        .Expect(0x570B7F, "83 C5 78 83 FB 20 0F 8C 25 FF FF FF 5D")
        .Expect(0x5702A7, "80 BF 72 30 81 00 00 75 20 8B 44 24 5C")  // PlaceMarker: reuse if !used
        .Expect(0x5702B4, "3B 87 74 30 81 00");                      //   and the identifier +0x54 matches
    s_markersOk = m.Ok();

    PatchGroup t("fixed: trace and money message layout");
    t.Expect(0x572904, "80 BD F0 08 81 00 00 0F 84 9C 0E 00 00")        // trace in use +0x18
        // age = TimeInMs - created +0x1C
        .Expect(0x572942, "8B 1D 34 3B 97 00 8B 85 F8 08 81 00 2B 9D F4 08 81 00")
        .Expect(0x5737B1, "83 C5 2C 83 7C 24 10 10 0F 8C 45 F1 FF FF")  //   16 x 0x2C
        .Expect(0x5746A0, "83 BD 70 F1 80 00 00")                       // money message registered +0 at 0x80F170
        .Expect(0x5746B1, "83 C5 3C 83 C6 3C 83 FB 10 7C E4")           //   16 x 0x3C
        .Expect(0x570F50, "8B 15 34 3B 97 00 53 89 CB 83 EC 38 2B 13 81 FA D0 07 00 00");
    s_timesOk = t.Ok();

    PatchGroup r("fixed: rubbish layout");
    r.Expect(0x567EC4, "6B DB 44 40")
        .Expect(0x567ED5, "81 C3 00 83 7F 00")                                       // sheets 0x44 at 0x7F8300
        .Expect(0x567EE7, "8A 43 1C 84 C0 0F 84 A5 04 00 00")
        .Expect(0x567EF2, "3C 01 BD 64 00 00 00")                                    //   state +0x1C
        .Expect(0x567EFB, "D9 03 DD DB D9 43 04 DD DA")
        .Expect(0x567F20, "D9 43 0C")                                                //   lying: +0, moving: +0xC
        .Expect(0x5687B0, "D9 05 F8 7C 69 00 D8 0D 2C 44 97 00 D8 46 34 D9 5E 34");  // spin: angle +0x34
    s_rubbishOk = r.Ok();

    PatchGroup gl("fixed: falling glass layout");
    gl.Expect(0x555D6B, "BE 10 87 7D 00")
        .Expect(0x555D72, "80 BD 7D 87 7D 00 00")                        // 45 x 0x70, active +0x6D
        .Expect(0x555D83, "83 C5 70 83 C6 70 83 FB 2D 7C E4 31 DB")
        .Expect(0x555F85, "8B 3D 34 3B 97 00 83 EC 78 8B 43 60 39 C7");  // start time +0x60
    s_glassOk = gl.Ok();

    PatchGroup c("fixed: water cannon layout");
    c.Expect(0x57AFD7, "BE 54 E3 97 00")
        .Expect(0x57AFE0, "83 BD 54 E3 97 00 00")                                // 3 x 0x19C, id +0
        .Expect(0x57AFF1, "81 C5 9C 01 00 00")
        .Expect(0x57B091, "0F BF B8 58 E3 97 00")                                // newest point +4
        .Expect(0x57B770, "0F BF 47 04")
        .Expect(0x57B79B, "80 BC 37 8C 01 00 00 00 0F 84 BE 02 00 00 0F BF EB")  // used +0x18C
        .Expect(0x57B7F6, "D9 44 07 0C D8 64 0F 0C")                             // points +0xC
        .Expect(0x57B739, "E8 62 D2 0C 00");                                     // rand()
    s_cannonsOk = c.Ok();

    PatchGroup ro("fixed: rope layout");
    ro.Expect(0x632C87, "BE 20 59 93 00")
        .Expect(0x632C90, "80 BD 20 59 93 00 00")                                    // 8 x 0x30C, active +0
        .Expect(0x632CA1, "81 C5 0C 03 00 00")
        .Expect(0x632CAD, "83 FB 08 7C DE 5D")
        .Expect(0x6328ED, "3B 82 24 59 93 00")                                       // id +4
        .Expect(0x6322D8, "D9 83 D0 00 00 00 D9 83 D4 00 00 00 D9 83 CC 00 00 00");  // point 16 (+0xC + 16 x 12)
    s_ropesOk = ro.Ok();

    PatchGroup f("fixed: plane banner and trail layout");
    f.Expect(0x54F29A, "BD A8 35 93 00 DE D9 89 E9 FF 74 24 04")
        .Expect(0x54F2AD, "81 C5 00 01 00 00 83 FB 06 7C E9")         // trails 6 x 0x100
        .Expect(0x54F2D5, "BD C8 33 93 00")
        .Expect(0x54F2E8, "83 C5 60 83 FB 05")                        // banners 5 x 0x60
        .Expect(0x54C4F4, "6B C0 60 D9 44 24 04 D9 98 C8 33 93 00");  // point 0 = the plane
    s_fluffOk = f.Ok();

    PatchGroup co("fixed: corona history layout");
    co.Expect(0x5434A7, "66 8B 04 45 64 41 81 00")
        .Expect(0x5434B4, "66 89 04 55 66 41 81 00 8B 44 24 04")                          // x +0x36
        .Expect(0x5434C5, "66 8B 04 45 70 41 81 00")
        .Expect(0x5434CD, "66 89 04 55 72 41 81 00 8D 04 39 8A 80 7D 41 81 00 8D 2C 3E")  // y +0x42
        .Expect(0x5434E1, "88 85 7E 41 81 00")
        .Expect(0x5434F3, "88 85 84 41 81 00")                                            // r +0x4E, g +0x54
        .Expect(0x543505, "88 85 8A 41 81 00")
        .Expect(0x543526, "88 85 90 41 81 00")                                            // b +0x5A, valid +0x60
        .Expect(0x543493, "C7 44 24 18 05 00 00 00 83 44 24 04 05 83 C2 05")
        .Expect(0x543532, "C6 87 90 41 81 00 00");
    s_coronasOk = co.Ok();

    PatchGroup su("fixed: sun layout");
    su.Expect(0x542100, "A1 00 C0 A0 00")
        .Expect(0x542118, "8D B2 78 1C 79 00 B8 C0 36 7E 00 A5 A5 A5")
        .Expect(0x54212A, "D8 0D A0 48 69 00")
        .Expect(0x542154, "D8 00")
        .Expect(0x5421F2, "6A 01 E8 87 01 00 00")
        .Expect(0x542270, "6A 02 E8 09 01 00 00")  // coronas 1, 2
        .Expect(0x542290, "E8 0B 55 03 00")
        .Expect(0x5422A0, "D9 1D D4 38 9B 00")
        .Expect(0x5422AA, "D9 1D D0 38 9B 00")
        .Expect(0x5425B1, "89 B1 3C 41 81 00")
        .Expect(0x5425E2, "D9 99 30 41 81 00");    // corona id +0xC, coors +0
    s_sunOk = su.Ok() && *(float*)kSunDistance == 150.0f;

    PatchGroup mv("fixed: moving building layout");
    mv.Expect(0x54F3B0, "66 83 BD 6A 28 7D 00 00 75 07 89 F1 E8 7F F7 FF FF")  // close ones updated every tick
        .Expect(0x54F3C1, "83 C6 1C 83 C5 1C 43")
        .Expect(0x54F3C8, "0F B7 15 44 F9 A0 00")
        .Expect(0x54EB48, "0F B7 43 08")
        .Expect(0x54F02F, "8B 4B 18 83 C1 04 E8 76 07 F9 FF")
        .Expect(0x54F07E, "66 83 7B 0A 01");
    s_movingOk = mv.Ok();

    PatchGroup sk("fixed: skidmark and antenna updates");
    sk.Expect(0x4A4397, "E8 54 BA 0C 00 E8 CF AB 09 00");
    bool skidOk = sk.Ok();

    // Render-path wrappers: each target has this single caller.
    bool hooked = fix::RetargetCall(0x4A63EA, 0x574680, (void*)SpecialFxRenderHook, "fixed: markers and effect clocks");
    if (!hooked) s_markersOk = s_timesOk = false;
    if (s_rubbishOk && !fix::RetargetCall(0x4A6408, 0x567E10, (void*)RubbishRenderHook, "fixed: rubbish presentation"))
        s_rubbishOk = false;
    if (s_glassOk && !fix::RetargetCall(0x4A63E0, 0x555CC0, (void*)GlassRenderHook, "fixed: glass presentation"))
        s_glassOk = false;
    if (s_cannonsOk &&
        !(fix::RetargetCall(0x4A63E5, 0x57AFD0, (void*)CannonsRenderHook, "fixed: water cannon presentation") &&
          fix::RetargetCall(0x57B739, 0x6489A0, (void*)CannonRandHook, "fixed: water cannon texture roll")))
        s_cannonsOk = false;
    if (s_ropesOk && !fix::RetargetCall(0x4A63EF, 0x632C80, (void*)RopesRenderHook, "fixed: rope presentation"))
        s_ropesOk = false;
    if (s_fluffOk &&
        !fix::RetargetCall(0x4A6426, 0x54F140, (void*)MovingThingsRenderHook, "fixed: plane banner presentation"))
        s_fluffOk = false;
    if (s_coronasOk && !fix::RetargetCall(0x4A640D, 0x5433F0, (void*)CoronasRenderHook, "fixed: light streaks"))
        s_coronasOk = false;
    if (skidOk) {
        fix::RetargetCall(0x4A4397, 0x56FDF0, (void*)SkidmarksUpdateHook, "fixed: skidmark update");
        fix::RetargetCall(0x4A439C, 0x53EF70, (void*)AntennasUpdateHook, "fixed: antenna update");
    }
    // Clock reads that are pure functions of time (masked, or clamped as signed), or only run while drawing.
    static const struct {
        uintptr_t at;
        const char* pattern;
        const char* what;
    } kClock[] = {{0x54EB63, "8B 35 34 3B 97 00", "fixed: lighthouse clock"},
                  {0x54EDD4, "8B 15 34 3B 97 00", "fixed: radar clock"},
                  {0x54EE8C, "8B 0D 34 3B 97 00", "fixed: fan clock"},
                  {0x54EF44, "8B 3D 34 3B 97 00", "fixed: blimp clock"},
                  {0x48796B, "03 0D 34 3B 97 00", "fixed: tree sway clock"},
                  {0x4879BD, "03 15 34 3B 97 00", "fixed: tree sway clock"},
                  {0x487A04, "8B 0D 34 3B 97 00", "fixed: tree sway clock"},
                  {0x57BE9E, "8B 15 34 3B 97 00", "fixed: rain streak clock"},
                  {0x57C212, "A1 34 3B 97 00", "fixed: rain streak start"},
                  {0x555625, "8B 15 34 3B 97 00", "fixed: glass fade clock"},
                  {0x5704DB, "8B 3D 34 3B 97 00", "fixed: marker pulse clock"}};
    bool clocks = true;
    for (int i = 0; i < 4; i++)
        clocks &= fix::Redirect(kClock[i].at, kClock[i].pattern, 0x973B34, s_clock, kClock[i].what);
    if (!clocks) s_movingOk = false;
    for (size_t i = 4; i < sizeof kClock / sizeof kClock[0]; i++)
        fix::Redirect(kClock[i].at, kClock[i].pattern, 0x973B34, s_clock, kClock[i].what);
    s_markerStep = game::TimeStep;
    fix::Redirect(0x570657, "D8 0D 2C 44 97 00", 0x97442C, &s_markerStep, "fixed: marker spin step");
    s_installed = true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  DRIVER INTERFACE
// ---------------------------------------------------------------------------------------------------------------------

void Reset() {
    for (auto& t : s_markers) t = MarkerTrack{};
    for (auto& t : s_sheets) t = SheetTrack{};
    for (auto& t : s_panes) t = PaneTrack{};
    for (auto& t : s_cannons) t = CannonTrack{};
    for (auto& t : s_ropes) t = RopeTrack{};
    s_fluff = History<Fluff>{};
    s_lastDrawValid = false;
    s_cannonRands = 0;
    if (s_installed) CaptureAll();
}

void BeforeTick(uint32_t index) {
    if (!s_installed) return;
    s_ticking = true;
    s_tickIndex = index;
    s_markerStep = game::TimeStep;
    if (s_markersOk) MarkersBeforeTick();
}

void AfterTick() {
    if (!s_installed) return;
    s_ticking = false;
    s_markerStep = game::TimeStep;
    if (s_markersOk) MarkersAfterTick();
    CaptureAll();
}

void BeginDraw(float alpha, uint32_t frameTicks) {
    if (!s_installed) return;
    EndDraw();
    s_drawing = true;
    s_alpha = alpha;
    s_frameTicks = frameTicks;
    s_tickCamera = *(Vec*)kCameraPos;
    uint32_t clock = *s_clock;
    int32_t step = s_lastDrawValid ? (int32_t)(clock - s_lastDrawClock) : 0;
    s_markerStep = std::clamp((float)step * 0.05f, 0.0f, 10.0f);  // TimeStep is 1/50 s
    s_lastDrawClock = clock;
    s_lastDrawValid = true;
    s_cannonCalls = 0;
    s_cannonRoll = frameTicks != 0;
    if (s_movingOk) ReposeBuildings();
}

void AfterCamera(Vec drawnCamera) {
    if (s_drawing && s_sunOk) PresentSun(drawnCamera);
}

void EndDraw() {
    if (!s_drawing) return;
    RestoreSun();
    RestoreBuildings();
    s_markerStep = game::TimeStep;
    s_drawing = false;
}

#ifdef VCUNLOCK_FIXED_TEST
bool LayoutsVerified() {
    return s_markersOk && s_timesOk && s_rubbishOk && s_glassOk && s_cannonsOk && s_ropesOk && s_fluffOk &&
           s_coronasOk && s_sunOk && s_movingOk;
}
#endif
}  // namespace fx
}  // namespace fixed
