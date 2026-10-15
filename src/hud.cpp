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
//   FILE      hud.cpp
//   MODULE    hud
//   ROLE      Widescreen field of view, aspect-correct HUD and the FPS counter
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Hor+ field of view for the 3D world and an aspect-correct 2D HUD. At 4:3 every value below equals the original
//   constant, so both features change nothing there.
//
//   Wide Screen and Show FPS switch at once from the menu: with Wide Screen off every value is the original one and
//   the view is stretched as in the original's fullscreen, also borderless and windowed. The FPS counter is always
//   installed but draws only when Show FPS is on.
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
#include "modules.h"
#include "timer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <span>

namespace hud {

// ---------------------------------------------------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------------------------------------------------

// Our copies of the values the patched instructions read, initialised to their 4:3 values.
static float g_hudScaleX = 1.0f / 640.0f;  // H/(480*W): replaces 1/640 in X-scaled HUD values
static float g_fovHorPlus = 45.0f;         // frustum FOV (degrees) matching the widened view window
static float g_sprite70w = 70.0f;          // CSprite::CalcScreenCoors width factor (70*k43)
static float g_arHorPlus = 4.0f / 3.0f;    // ms_fAspectRatio * kx for camera code
static uint8_t s_noWidePref = 0;           // CalculateAspectRatio reads this instead of the menu pref

static double s_kx = 1.0;  // horizontal widening = A/(4/3)
static int s_w, s_h;
static bool s_fov, s_hud, s_fpsHook;  // patches in place; g_cfg.widescreen and g_cfg.showFps switch them at runtime

static double Widening() { return s_fov && g_cfg.widescreen ? s_kx : 1.0; }

// ---------------------------------------------------------------------------------------------------------------------
//  VIEW SIZE AND FIELD OF VIEW
// ---------------------------------------------------------------------------------------------------------------------

static void UpdateFov() {
    float fov = game::MsFov();
    double kx = Widening();
    g_fovHorPlus = kx == 1.0 ? fov
                             : (float)(2.0 * atan(tan(fov * 0.5 * (3.14159265358979 / 180.0)) * kx) *
                                       (180.0 / 3.14159265358979));
}

// With widescreen off every copy holds the original value.
static void Recompute() {
    bool hud = s_hud && g_cfg.widescreen && s_w > 0 && s_h > 0;
    g_hudScaleX = hud ? (float)(s_h / (480.0 * s_w)) : 1.0f / 640.0f;
    double kx = Widening();
    g_sprite70w = (float)(70.0 / kx);
    g_arHorPlus = (float)(game::MsAspectRatio() * kx);
    UpdateFov();
}

static void SetSize(int w, int h) {
    if (w == s_w && h == s_h) return;
    s_w = w;
    s_h = h;
    s_kx = (3.0 * w) / (4.0 * h);
    Recompute();
    Log("hud: %dx%d, kx %.4f, hudScaleX %.7f, fov %.2f -> %.2f", w, h, s_kx, g_hudScaleX, (float)game::MsFov(),
        g_fovHorPlus);
}

bool WidescreenReady() { return s_fov || s_hud; }
bool FpsReady() { return s_fpsHook; }

// Widescreen switched in the game menu: the camera view window follows on the next CameraSize (every frame).
void ApplyWidescreen() {
    Recompute();
    Log("hud: widescreen %s", g_cfg.widescreen ? "on" : "off");
}

// ---------------------------------------------------------------------------------------------------------------------
//  CAMERA HOOKS
// ---------------------------------------------------------------------------------------------------------------------

// CameraSize 0x580729, after the camera rasters were resized; ebx = RwRect* (w at +8, h at +0xC)
static void __cdecl OnCameraSize(fix::Regs* r, fix::Fpu*) {
    const int32_t* rect = (const int32_t*)r->ebx;
    if (rect[2] > 0 && rect[3] > 0) SetSize(rect[2], rect[3]);
    uint32_t* s = r->Stack();
    float V = *(float*)&s[0x4C / 4];   // tan(hfov43/2)
    float ar = *(float*)&s[0x50 / 4];  // 4/3, or 5/4 with the cutscene letterbox
    float* vw = (float*)&s[2];
    if (!s_fov || !g_cfg.widescreen) {
        // The original's exclusive fullscreen branch in every display mode: the view stretched across the screen.
        // Its branch for other video modes (0x580748) would crop the view to the window's shape instead.
        vw[0] = V;
        vw[1] = V / ar;
    } else {
        if (ar > 1.5f) ar *= 0.75f;  // menu widescreen pref (16/9, 5/3) -> 4/3, 5/4
        vw[0] = (float)(V * s_kx);   // Hor+: wider horizontally
        vw[1] = V / ar;              // original 4:3 vertical extent
    }
    r->cont = At(0x58078C);  // RwCameraSetViewWindow(cam, vw)
}

// CalculateAspectRatio tail (fld ms_fAspectRatio; ret): keep the camera code's aspect in step
static void __cdecl OnAspectRatio(fix::Regs*, fix::Fpu*) { g_arHorPlus = (float)(game::MsAspectRatio() * Widening()); }

// Replaces CDraw::SetFOV (fld [esp+4]; fstp ms_fFOV; ret), the only writer of ms_fFOV
static float* s_msFov;  // &game::MsFov(), set before SetFOV is installed
__declspec(naked) static void SetFOV() {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, s_msFov
        fstp dword ptr [eax]
        pushad
        pushfd
        sub esp, 108
        fsave [esp]
        call UpdateFov
        frstor [esp]
        add esp, 108
        popfd
        popad
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  HUD SITES
// ---------------------------------------------------------------------------------------------------------------------

// An fmul that reads one of the game's 1/640 copies; see hud_sites.inc. A site one executable does not have names it.
struct Site {
    uint32_t addr, copy;
    int absentIn = -1;  // a ver::Id
};

static const Site kHudSites[] = {
#include "hud_sites.inc"
};

// The sites of the running executable: the rows it has, through the map, and its own ones.
struct Here {
    uintptr_t at, copy;
    uint32_t row;  // the Steam address of the row, 0 for a site of this executable only
};

static int HereSites(Here* out) {
    int n = 0;
    for (const Site& s : kHudSites)
        if (s.absentIn < 0 || !ver::Is(ver::Id(s.absentIn))) out[n++] = {At(s.addr), At(s.copy), s.addr};
    // The Japanese CHud::Draw moves one centred text right by 15 units, 0x17 bytes after the counterpart of 0x55B917.
    if (ver::Is(ver::JP)) out[n++] = {At(0x55B917) ? At(0x55B917) + 0x17 : 0, At(0x696A78), 0};
    return n;
}

// Taken: still reads the game's copy. Left: another mod (SilentPatch: the radar blur exclusion) points it at its own
// constant, so that element stays the mod's. Anything else is a different executable.
enum class SiteState { Taken, Left, Bad };

static HMODULE SiteOwner(const Here& s) {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)*(const uint32_t*)(s.at + 2), &m);
    return m;
}

static SiteState HudSite(const Here& s) {
    const uint8_t* b = (const uint8_t*)s.at;
    if (!b || b[0] != 0xD8 || b[1] != 0x0D) return SiteState::Bad;
    if (*(const uint32_t*)(b + 2) == s.copy) return SiteState::Taken;
    HMODULE m = SiteOwner(s);
    return m && m != GetModuleHandleA(nullptr) ? SiteState::Left : SiteState::Bad;
}

// ---------------------------------------------------------------------------------------------------------------------
//  FPS COUNTER
// ---------------------------------------------------------------------------------------------------------------------

// Printed with the game's font right before the frame's last font flush (Idle 0x4A600D,
// after the HUD and fades); hidden while the pause menu is open. CFont::Details is saved and restored.
constexpr uintptr_t kFontDetails = 0x97E828;  // CFont::Details, 0x54 bytes
constexpr uint32_t kFontStrings = 0x50;       // its last field, the per-frame string counter
constexpr size_t kFontCopy = 0x60;            // room for the copy (the Japanese Details is 0x57 bytes to that field)
constexpr GameFn<void(__cdecl*)()> DrawFonts{0x550140};
constexpr GameFn<void(__cdecl*)(float, float, const uint16_t*)> PrintString{0x550F30};
static double s_fpsStart;
static int s_fpsFrames, s_fps;

// Each field goes through the map: the Japanese Details has a byte before the colour, so its fields are not at one
// shift.
template <class T> static T& Font(uint32_t off) { return *(T*)At(kFontDetails + off); }
static uint8_t* FontDetails() { return (uint8_t*)At(kFontDetails); }
static size_t FontDetailsSize() { return At(kFontDetails + kFontStrings) + 4 - At(kFontDetails); }

static void __cdecl DrawFontsWithFps() {
    double now = timer::Now();
    ++s_fpsFrames;
    if (now - s_fpsStart >= 0.5) {
        if (s_fpsStart > 0) s_fps = (int)(s_fpsFrames / (now - s_fpsStart) + 0.5);
        s_fpsStart = now;
        s_fpsFrames = 0;
    }
    if (g_cfg.showFps && s_fps > 0 && !game::MenuActive() && game::RsGlobalHeight() > 0) {  // HUD only, not the menu
        uint8_t saved[kFontCopy];
        size_t size = FontDetailsSize();
        memcpy(saved, FontDetails(), size);
        float h = (float)game::RsGlobalHeight();
        static const uint8_t color[4] = {255, 255, 255, 230}, shadow[4] = {0, 0, 0, 230};
        memcpy(&Font<uint8_t>(0x00), color, 4);
        Font<float>(0x04) = 0.45f * h / 480.0f;  // X scaled by height, like the aspect-correct HUD
        Font<float>(0x08) = 0.75f * h / 448.0f;
        Font<float>(0x0C) = 0;               // slant
        memset(&Font<uint8_t>(0x18), 0, 9);  // justify, centre, right, background(s), prop, shadow, flash, bold
        Font<uint8_t>(0x1D) = 1;             // proportional
        Font<float>(0x24) = 255.0f;          // no alpha fade
        Font<float>(0x2C) = (float)game::RsGlobalWidth();  // wrap
        Font<int16_t>(0x38) = 1;                           // FONT_STANDARD
        Font<uint8_t>(0x3A) = 0;
        Font<int16_t>(0x40) = 2;  // drop shadow
        memcpy(&Font<uint8_t>(0x42), shadow, 4);
        char text[16];
        int n = sprintf_s(text, "%d FPS", s_fps);
        uint16_t wide[16];
        for (int i = 0; i <= n; ++i) wide[i] = (uint8_t)text[i];
        PrintString(6.0f * h / 480.0f, 4.0f * h / 448.0f, wide);
        uint32_t strings = Font<uint32_t>(kFontStrings);  // keep PrintString's increment
        memcpy(FontDetails(), saved, size);
        Font<uint32_t>(kFontStrings) = strings;
    }
    DrawFonts();
}

static void InstallFps() {
    PatchGroup g("hud: FPS counter");
    g.Expect(At(0x4A600D), "E8 <550140>")                               // Idle: CFont::DrawFonts after menus and fade
        .Expect(At(0x550140), "E8 <551920> C3")                         // CFont::DrawFonts
        // CFont::PrintString(x, y, wchar*); the Japanese one first leaves out strings that start with '*'
        .Expect(At(0x550F30), ver::Is(ver::JP) ? "53 83 EC 10 8B 5C 24 20" : "53 56 57 55 83 EC 38 8B 74 24 54")
        // The CFont::Details fields DrawFontsWithFps writes directly, as the CFont setters write them:
        .Expect(At(0x55006A), "88 15 [97E828] 8A 43 01 A2 [97E829]")    // SetColor: +0
        // SetScale: +4, +8
        .Expect(At(0x550120), "D9 44 24 04 D9 1D [97E82C] D9 44 24 08 D9 1D [97E830]")
        .Expect(At(0x5500F0), "D9 44 24 04 D9 1D [97E834]")              // SetSlant: +0xC
        // SetJustifyOn: +0x18..+0x1A
        .Expect(At(0x550040), "C6 05 [97E840] 01 C6 05 [97E841] 00 C6 05 [97E842] 00")
        .Expect(At(0x54FF10), "C6 05 [97E845] 01")                      // SetPropOn: +0x1D
        .Expect(At(0x54FEB0), "D9 44 24 04 D9 1D [97E84C]")             // SetAlphaFade: +0x24
        .Expect(At(0x54FFF0), "D9 44 24 04 D9 1D [97E854]")             // SetWrapx: +0x2C
        // SetFontStyle: +0x38, +0x3A
        .Expect(At(0x54FEE5), "66 C7 05 [97E860] 01 00 C6 05 [97E862] 01")
        .Expect(At(0x54FE10), "8B 44 24 04 66 A3 [97E868]")             // SetDropShadowPosition: +0x40
        .Expect(At(0x54FE2A), "88 15 [97E86A]");                        // SetDropColor: +0x42
    if (FontDetailsSize() > kFontCopy) {
        Log("[hud: FPS counter] CFont::Details is %u bytes here", (unsigned)FontDetailsSize());
        g.ok = false;
    }
    if (g.Ok()) s_fpsHook = fix::RetargetCall(At(0x4A600D), At(0x550140), (void*)&DrawFontsWithFps, "hud: FPS counter");
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void Install() {
    InstallFps();
    s_fov = s_hud = true;

    PatchGroup g("hud: camera size hook");
    g.Expect(At(0x580729), "8B 44 24 1C 83 E0 01 74 16 D9 44 24 4C D8 74 24 50");
    if (!g.Ok()) {
        Log("hud: widescreen disabled (CameraSize mismatch)");
        s_fov = s_hud = false;
        return;
    }

    if (s_fov) {
        PatchGroup f("hud: Hor+ FOV");
        f.Expect(At(0x54A1D0), "D9 44 24 04 D9 1D [695660] C3");    // CDraw::SetFOV
        f.Expect(At(0x54A160), "80 3D [86865A] 00");                // CalculateAspectRatio: widescreen pref
        f.Expect(At(0x54A1BA), "D9 05 [94CD40] C3");                // CalculateAspectRatio tail
        f.Expect(At(0x46C2D0), "D8 0D [695660]");                   // frustum: fmul ms_fFOV
        f.Expect(At(0x57785D), "D9 05 [698518] D8 35 [695660]");    // CalcScreenCoors: fld 70.0 (width only)
        static const uintptr_t kAspect[] = {0x46F81D, 0x473EAA, 0x47C690, 0x47C963};
        for (uintptr_t a : kAspect) f.Expect(At(a), "D8 0D [94CD40]");
        if (f.Ok()) {
            s_msFov = &game::MsFov();
            mem::Jmp(At(0x54A1D0), (void*)&SetFOV);
            fix::Redirect(At(0x54A160), "80 3D [86865A] 00", At(0x86865A), &s_noWidePref,
                          "aspect: ignore widescreen pref");
            fix::Hook(At(0x54A1BA), 6, "D9 05 [94CD40]", OnAspectRatio, "aspect: Hor+ copy");
            fix::Redirect(At(0x46C2D0), "D8 0D [695660]", At(0x695660), &g_fovHorPlus, "frustum FOV");
            fix::Redirect(At(0x57785D), "D9 05 [698518]", At(0x698518), &g_sprite70w, "sprite width");
            for (uintptr_t a : kAspect)
                fix::Redirect(At(a), "D8 0D [94CD40]", At(0x94CD40), &g_arHorPlus, "camera aspect");
        } else s_fov = false;
    }

    if (s_hud) {
        Here sites[std::size(kHudSites) + 1];
        int count = HereSites(sites), bad = 0, left = 0;
        for (const Here& s : std::span(sites, count)) {
            SiteState st = HudSite(s);
            if (st == SiteState::Left) {
                char mod[MAX_PATH];
                GetModuleFileNameA(SiteOwner(s), mod, MAX_PATH);
                const char* n = strrchr(mod, '\\');
                Log("hud: site %08X left to %s", s.row ? s.row : s.at, n ? n + 1 : mod);
                ++left;
            } else if (st == SiteState::Bad && bad++ < 5)
                Log("hud: site %08X does not match (copy %08X)", s.row ? s.row : s.at, s.copy);
        }
        if (bad) {
            Log("hud: aspect-correct HUD SKIPPED (%d of %d sites differ)", bad, count);
            s_hud = false;
        } else {
            for (const Here& s : std::span(sites, count))
                if (HudSite(s) == SiteState::Taken) mem::Put<uint32_t>(s.at + 2, (uint32_t)(uintptr_t)&g_hudScaleX);
            Log("hud: aspect-correct HUD applied (%d sites)", count - left);
        }
    }

    if ((s_fov || s_hud) && !fix::Hook(At(0x580729), 7, "8B 44 24 1C 83 E0 01", OnCameraSize, "CameraSize view window"))
        s_fov = s_hud = false;
}

}  // namespace hud
