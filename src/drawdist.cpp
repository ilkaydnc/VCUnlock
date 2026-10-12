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
//   FILE      drawdist.cpp
//   MODULE    drawdist
//   ROLE      Draw distance and building fade-in
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   - The Draw Distance slider scales every object's draw distance, but CRenderer only looks for detailed objects within
//     a fixed 300 m. Above that the low-detail model is drawn, so most of the slider did nothing. The 300 m now follows
//     the same multiplier (never below 300 m). Its limit is set for the area scanned: zooming in narrows the view and
//     allows a longer distance.
//   - A longer scan finds more objects, so three fixed sizes grow with it: the visible list (2000 entries, written
//     without a bounds check) moves to a larger array with one, the sorted list of fading and see-through objects
//     grows from 200 entries, and the streaming memory from 45 MB.
//   - Buildings fade in by 16/255 on every visibility check, once per drawn frame, so at 144 fps a fade took 0.11 s
//     instead of 0.53 s. The step now follows the frame's real length.
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
#include "drawdist.h"
#include <cmath>
#include <cstring>

namespace drawdist {

// ---------------------------------------------------------------------------------------------------------------------
//  ADDRESSES AND SIZES
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t kLodDistance = 0x68F234;    // CRenderer's 300 m (LOD_DISTANCE); only CRenderer reads it
constexpr uintptr_t kLodMultiplier = 0x46DDAF;  // CCamera::Process, just after LODDistMultiplier [ebx+0xF0] is stored
constexpr uintptr_t kVisibleList = 0x7D4500;    // CRenderer::ms_aVisibleEntityPtrs[2000]
constexpr uintptr_t kVisibleCount = 0xA0C1EC;   // CRenderer::ms_nNoOfVisibleEntities
constexpr uintptr_t kAlphaListSize = 0x58270C;  // CVisibilityPlugins::Initialise: push 200 (m_alphaEntityList)
constexpr uintptr_t kStreamingSize = 0x41079F;  // CStreaming::Init: ms_memoryAvailable = 45 MB

constexpr float kOriginalLod = 300.0f;
constexpr int kVisibleMax = 16384;
constexpr uint32_t kAlphaEntities = 1000;
constexpr uint32_t kStreamingBytes = 192u << 20;

bool g_lodFix = true, g_fadeFix = true;
float g_lodLimit = 600.0f;
float g_lodDistance = kOriginalLod;
uint32_t g_fadeStep = 16;
uint32_t g_visibleLost;

static void* s_visible[kVisibleMax];
static bool s_ready, s_moved;
static double s_fadeCarry;

bool Ready() { return s_ready; }
void* const* VisibleList() { return s_moved ? s_visible : (void* const*)kVisibleList; }
int VisibleCapacity() { return s_moved ? kVisibleMax : 2000; }

// ---------------------------------------------------------------------------------------------------------------------
//  CODE EMITTER
// ---------------------------------------------------------------------------------------------------------------------

struct Emit {
    uint8_t* p;
    void B(uint8_t b) { *p++ = b; }
    void U32(uint32_t v) { memcpy(p, &v, 4); p += 4; }
    void Jmp(uintptr_t t) { B(0xE9); U32((uint32_t)(t - ((uintptr_t)p + 4))); }
};

// ---------------------------------------------------------------------------------------------------------------------
//  SCAN DISTANCE
// ---------------------------------------------------------------------------------------------------------------------

// LODDistMultiplier = 70/FOV, raised above 55 m in aircraft (at most 2.2), times the Draw Distance slider. The world
// scan is a triangle in front of the camera, so its area grows with distance squared times the view's width; the
// limit keeps that area at most what g_lodLimit gives at a 70 degree field of view.
void SetScanDistance(float m) {
    float d = kOriginalLod * m;
    float fov = game::MsFov;
    if (fov < 5.0f) fov = 5.0f;
    float limit = g_lodLimit * sqrtf(tanf(35.0f * 0.017453293f) / tanf(fov * 0.5f * 0.017453293f));
    if (d > limit) d = limit;
    if (!g_lodFix || !(d > kOriginalLod)) d = kOriginalLod;
    g_lodDistance = d;
    *(float*)kLodDistance = d;
}

static void __cdecl OnLodMultiplier(fix::Regs* r, fix::Fpu*) { SetScanDistance(*(float*)(r->ebx + 0xF0)); }

// ---------------------------------------------------------------------------------------------------------------------
//  VISIBLE LIST
// ---------------------------------------------------------------------------------------------------------------------

// The three inserts: mov eax,[count]; inc [count]; mov [eax*4+list], reg. The thunk checks the count first.
struct Insert {
    uintptr_t at;
    const char* bytes;
};
static const Insert kInserts[] = {
    {0x4C74D3, "A1 EC C1 A0 00 FF 05 EC C1 A0 00 89 2C 85 00 45 7D 00"},  // ScanSectorList
    {0x4C76AD, "A1 EC C1 A0 00 FF 05 EC C1 A0 00 89 2C 85 00 45 7D 00"},  // ScanSectorList_Priority
    {0x4C846D, "A1 EC C1 A0 00 FF 05 EC C1 A0 00 89 34 85 00 45 7D 00"},  // ScanBigBuildingList
};
static const Insert kReaders[] = {
    {0x4C9E40, "8B 34 AD 00 45 7D 00"},  // render pass over the visible list
    {0x4CA078, "8B 0C 9D 00 45 7D 00"},  // render pass over the visible list
    {0x4CA0C0, "8B 0C AD 00 45 7D 00"},  // CRenderer::PreRender
};

static uint8_t* InsertThunk(const Insert& in) {
    uint8_t* t = fix::AllocCode(48);
    if (!t) return nullptr;
    uintptr_t back = in.at + 18;
    uint8_t modrm = *(const uint8_t*)(in.at + 12);  // 0x2C ebp, 0x34 esi
    Emit e{t};
    e.B(0xA1); e.U32(kVisibleCount);                          // mov eax, [count]
    e.B(0x3D); e.U32(kVisibleMax);                            // cmp eax, max
    e.B(0x73); e.B(18);                                       // jae full
    e.B(0xFF); e.B(0x05); e.U32(kVisibleCount);               // inc dword [count]
    e.B(0x89); e.B(modrm); e.B(0x85); e.U32((uint32_t)(uintptr_t)s_visible);  // mov [eax*4+list], reg
    e.Jmp(back);
    e.B(0xFF); e.B(0x05); e.U32((uint32_t)(uintptr_t)&g_visibleLost);         // full: inc dword [lost]
    e.Jmp(back);
    FlushInstructionCache(GetCurrentProcess(), t, e.p - t);
    return t;
}

// ---------------------------------------------------------------------------------------------------------------------
//  FADE-IN
// ---------------------------------------------------------------------------------------------------------------------

// CSimpleModelInfo::IncreaseAlpha, inlined: alpha = alpha >= 0xEF ? 0xFF : alpha + 16. The thunk adds g_fadeStep
// with the same ceiling, so a step of 16 gives the original result.
struct Fade {
    uintptr_t at;
    const char* bytes;
    uint8_t base;  // register holding the model info: 5 ebp, 3 ebx
    uintptr_t next;
};
static const Fade kFades[] = {
    {0x4C79FC, "80 7D 41 EF 73 06 80 45 41 10 EB 04 C6 45 41 FF", 5, 0x4C7A0C},              // big buildings
    {0x4C7AF4, "80 7D 41 EF 73 06 80 45 41 10 EB 04 C6 45 41 FF", 5, 0x4C7B04},              // big buildings, fading
    {0x4C81C1, "80 7B 41 EF 73 09 80 43 41 10 EB 07 8D 40 00 C6 43 41 FF", 3, 0x4C81D4},     // entities
    {0x4C8320, "80 7B 41 EF 73 0A 80 43 41 10 EB 08 8D 44 20 00 C6 43 41 FF", 3, 0x4C8334},  // entities, fading
};

static bool FadeThunk(const Fade& f) {
    uint8_t* t = fix::AllocCode(48);
    if (!t) return false;
    uint8_t m = uint8_t(0x40 | f.base);  // [base+disp8], eax/al
    Emit e{t};
    e.B(0x50);                                                // push eax
    e.B(0x0F); e.B(0xB6); e.B(m); e.B(0x41);                  // movzx eax, byte [base+0x41]
    e.B(0x03); e.B(0x05); e.U32((uint32_t)(uintptr_t)&g_fadeStep);  // add eax, [step]
    e.B(0x3D); e.U32(0xFF);                                   // cmp eax, 0xFF
    e.B(0x76); e.B(5);                                        // jbe store
    e.B(0xB8); e.U32(0xFF);                                   // mov eax, 0xFF
    e.B(0x88); e.B(m); e.B(0x41);                             // store: mov [base+0x41], al
    e.B(0x58);                                                // pop eax
    e.Jmp(f.next);
    FlushInstructionCache(GetCurrentProcess(), t, e.p - t);
    mem::Jmp(f.at, t, f.next - f.at);
    fix::Count(true);
    return true;
}

// 16 per 1/30 s of real time, the original step at 30 fps; the fraction carries over to the next frame.
void UpdateFadeStep() {
    if (!g_fadeFix) {
        g_fadeStep = 16;
        s_fadeCarry = 0;
        return;
    }
    double step = s_fadeCarry + 16.0 * timer::kFrame;
    if (step > 255.0) step = 255.0;
    g_fadeStep = (uint32_t)step;
    s_fadeCarry = step - g_fadeStep;
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void Install(bool world) {
    PatchGroup g("drawdist: scan distance, render lists and streaming memory");
    g.Expect(kLodDistance, "00 00 96 43")
        .Expect(0x46DD9D, "D9 83 F0 00 00 00 D8 0D 28 F2 68 00 D9 9B F0 00 00 00 8B 83 08 08 00 00")
        .Expect(kAlphaListSize - 16, "B9 8C ED A0 00 C7 05 04 E3 97 00 00 00 00 00 68 C8 00 00 00")
        .Expect(kStreamingSize - 6, "C7 05 5C CD 94 00 00 00 D0 02");
    for (const Insert& in : kInserts) g.Expect(in.at, in.bytes);
    for (const Insert& rd : kReaders) g.Expect(rd.at, rd.bytes);
    uint8_t* thunks[3] = {};
    for (int i = 0; i < 3; ++i) thunks[i] = InsertThunk(kInserts[i]);
    if (!thunks[0] || !thunks[1] || !thunks[2]) g.ok = false;
    // The list moves whole (all inserts and readers) before the scan grows
    if (g.Ok()) {
        for (int i = 0; i < 3; ++i) {
            mem::Jmp(kInserts[i].at, thunks[i], 18);
            fix::Count(true);
        }
        for (const Insert& rd : kReaders) fix::Redirect(rd.at, rd.bytes, kVisibleList, s_visible, "drawdist: visible list");
        s_moved = true;
        mem::Put<uint32_t>(kAlphaListSize, kAlphaEntities);
        mem::Put<uint32_t>(kStreamingSize, kStreamingBytes);
        s_ready = fix::Hook(kLodMultiplier, 6, "8B 83 08 08 00 00", OnLodMultiplier, "drawdist: scan distance");
        Log("drawdist: scan distance up to %.0f m, visible list %d, sorted list %u, streaming %u MB", g_lodLimit,
            kVisibleMax, kAlphaEntities, kStreamingBytes >> 20);
    }
    if (!world) return;
    PatchGroup f("drawdist: building fade-in");
    for (const Fade& fd : kFades) f.Expect(fd.at, fd.bytes);
    if (!f.Ok()) return;
    for (const Fade& fd : kFades) FadeThunk(fd);
    timer::OnDrawnFrame(UpdateFadeStep);
}
}  // namespace drawdist
