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
//   ROLE      The FPS counter
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The frame rate, printed with the game's font in the top-left corner. The counter is always installed but draws
//   only when Show FPS is on, so the menu row switches it at once.
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
#include <cstdio>
#include <cstring>

namespace hud {

static bool s_fpsHook;  // patch in place; g_cfg.showFps switches the counter at runtime

bool FpsReady() { return s_fpsHook; }

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
        Font<float>(0x04) = 0.45f * h / 480.0f;  // X scaled by height: the same shape on any screen
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

void Install() {
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

}  // namespace hud
