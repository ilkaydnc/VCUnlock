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
//   FILE      menu.cpp
//   MODULE    menu
//   ROLE      The frame-rate settings as rows of Display Setup
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The settings live in the game's own Display Setup, drawn with the game's font and named in its five languages:
//
//   - Frame Limiter: VSYNC, 30, 60, 120, 144, 165, 240 or OFF.
//   - Show FPS, a new row above Restore Defaults.
//
//   Three hooks do the work: the value switch of DrawStandardMenus draws our values, and the Enter and left/right
//   switches of ProcessUserInput change them. CText::Get names the new row. Changes apply at once and are written to
//   VCUnlock.ini.
//
//   The two rows are checked apart: if another mod has changed the rows below Frame Limiter, Show FPS is left out and
//   Frame Limiter still works.
//
//   The game's own Frame Limiter setting stays OFF while VCUnlock runs. The game no longer reads it, but other mods do:
//   the Widescreen Fix takes it as its own limiter and would hold the game at 30 fps. gta_vc.set keeps the player's
//   value, because loading and saving it go to a copy.
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
#include "render.h"
#include "modules.h"
#include <cstdio>
#include <cstring>

namespace menu {

// ---------------------------------------------------------------------------------------------------------------------
//  MENU TABLE
// ---------------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct Entry {
    int16_t action;
    char name[8];
    uint8_t saveSlot, targetScreen;
    uint16_t x, y;  // 0 = below the previous row
    uint8_t align, pad;
};
#pragma pack(pop)
static_assert(sizeof(Entry) == 18, "menu entry");

enum Screen { DisplaySetup = 4 };
// Actions of the rows taken over, and a number the game doesn't use for the new row
enum Action : int16_t {
    FrameLimiter = 8,
    RestoreDefaults = 47,
    ShowFps = 60,
};

static Entry* Entries(int screen) { return (Entry*)(At(game::MenuScreens) + screen * 0xE2 + 10); }

// The rows as the game has them: Frame Limiter is row 2, Restore Defaults and Back are rows 9 and 10, row 11 is free.
static const Entry kLimiterRow = {FrameLimiter, "FEM_FRM", 0, DisplaySetup, 40, 128, 1, 0};
static const Entry kDisplayOldTail[3] = {
    {RestoreDefaults, "FET_DEF", 0, DisplaySetup, 320, 303, 3, 0},
    {34, "FEDS_TB", 0, 33, 320, 328, 3, 0},
    {},
};

// Display Setup gains Show FPS above Restore Defaults; the page already uses 25-unit rows.
static const Entry kDisplayTail[3] = {
    {ShowFps, "FED_FPS", 0, DisplaySetup, 40, 303, 1, 0},
    {RestoreDefaults, "FET_DEF", 0, DisplaySetup, 320, 328, 3, 0},
    {34, "FEDS_TB", 0, 33, 320, 353, 3, 0},
};

// ---------------------------------------------------------------------------------------------------------------------
//  TEXT
// ---------------------------------------------------------------------------------------------------------------------

// Written like the game's own menu text: capitals, French without accents.
static const char* const kShowFps[5] = {"SHOW FPS", "AFFICHER LES FPS", "FPS ANZEIGEN", "MOSTRA FPS", "MOSTRAR FPS"};

static void Widen(uint16_t* out, size_t n, const char* s) {
    size_t i = 0;
    for (; s[i] && i + 1 < n; ++i) out[i] = (uint8_t)s[i];
    out[i] = 0;
}

static const uint16_t*(__thiscall* s_textGet)(void*, const char*);

static const uint16_t* GameText(const char* key) { return s_textGet((void*)At(game::TheText), key); }

// CText::Get: the label of the new row.
static const uint16_t* __fastcall TextGet(void* text, void*, const char* key) {
    if (!strcmp(key, "FED_FPS")) {
        static uint16_t buf[40];
        int l = game::PrefsLanguage();
        Widen(buf, 40, kShowFps[l >= 0 && l < 5 ? l : 0]);
        return buf;
    }
    return s_textGet(text, key);
}

// ---------------------------------------------------------------------------------------------------------------------
//  VALUES
// ---------------------------------------------------------------------------------------------------------------------

static bool s_limiter, s_fps;

// Frame Limiter: VSYNC, the caps, OFF. A cap set in the ini that is not in the list is shown as it is.
static const int kCaps[] = {30, 60, 120, 144, 165, 240};
constexpr int kNumSteps = 2 + (int)(sizeof kCaps / sizeof kCaps[0]);  // VSYNC, caps, OFF

static void SetStep(int step) {
    g_cfg.vsync = step == 0;
    g_cfg.fpsLimit = step > 0 && step < kNumSteps - 1 ? kCaps[step - 1] : 0;
}

static void StepLimiter(int dir) {
    int step;
    if (g_cfg.vsync) step = 0;
    else if (g_cfg.fpsLimit <= 0) step = kNumSteps - 1;
    else {
        step = -1;
        for (int i = 0; i < kNumSteps - 2; ++i)
            if (kCaps[i] == g_cfg.fpsLimit) step = i + 1;
        if (step < 0) {  // between two caps: move to the neighbour in that direction
            int i = 0;
            while (i < kNumSteps - 2 && kCaps[i] < g_cfg.fpsLimit) ++i;
            SetStep(dir > 0 ? (i < kNumSteps - 2 ? i + 1 : kNumSteps - 1) : i);
            return;
        }
    }
    SetStep((step + (dir > 0 ? 1 : kNumSteps - 1)) % kNumSteps);
}

static const uint16_t* ValueText(int action) {
    static uint16_t wide[16];
    switch (action) {
        case FrameLimiter:
            if (!s_limiter) return nullptr;
            if (g_cfg.vsync) Widen(wide, 16, "VSYNC");
            else if (g_cfg.fpsLimit > 0) {
                char s[16];
                sprintf_s(s, "%d", g_cfg.fpsLimit);
                Widen(wide, 16, s);
            } else return GameText("FEM_OFF");
            return wide;
        case ShowFps: return s_fps ? GameText(g_cfg.showFps ? "FEM_ON" : "FEM_OFF") : nullptr;
    }
    return nullptr;
}

// Left, right, mouse wheel or Enter on a row. True if the row is ours.
static bool Change(int action, int dir) {
    switch (action) {
        case FrameLimiter:
            if (!s_limiter) return false;
            StepLimiter(dir);
            render::ApplyVsync();
            break;
        case ShowFps:
            if (!s_fps) return false;
            g_cfg.showFps = !g_cfg.showFps;
            break;
        default: return false;
    }
    SaveConfig();
    return true;
}

static void RestoreOurDefaults() {
    Config def;
    if (s_limiter) {
        g_cfg.vsync = def.vsync;
        g_cfg.fpsLimit = def.fpsLimit;
        render::ApplyVsync();
    }
    if (s_fps) g_cfg.showFps = def.showFps;
    SaveConfig();
}

// ---------------------------------------------------------------------------------------------------------------------
//  HOOKS
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t kDrawValue = 0x49EA02;  // movsx eax, [esi + entries]: the value switch of DrawStandardMenus
constexpr uintptr_t kDrawText = 0x49EDF4;   // ebx = value text, draws it
constexpr uintptr_t kEnter = 0x49956D;      // movsx ebx, [edi + entries]: the Enter switch of ProcessUserInput
constexpr uintptr_t kEnterDone = 0x499BDA;  // after the switch and ProcessOnOffMenuOptions
constexpr uintptr_t kChange = 0x499CAA;     // movsx eax, [edi*2 + entries]: the left/right switch
constexpr uintptr_t kChangeDone = 0x499F03;  // after CheckSliderMovement and ProcessOnOffMenuOptions

static void __cdecl OnDrawValue(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->esi + At(game::MenuScreens) + 10);
    const uint16_t* text = ValueText(action);
    if (!text) return;
    r->ebx = (uint32_t)(uintptr_t)text;
    r->cont = At(kDrawText);
}

static void __cdecl OnEnter(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->edi + At(game::MenuScreens) + 10);
    if (action == RestoreDefaults) {  // and the game's own defaults
        if (game::MenuCurrScreen() == DisplaySetup) RestoreOurDefaults();
    } else if (Change(action, 1)) r->cont = At(kEnterDone);
}

static void __cdecl OnChange(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->edi * 2 + At(game::MenuScreens) + 10);
    int8_t dir = *((int8_t*)r->Stack() + 0x84);  // changeAmount
    if (Change(action, dir)) r->cont = At(kChangeDone);
}

// ---------------------------------------------------------------------------------------------------------------------
//  THE GAME'S OWN SETTING
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t kLoadSettings = 0x48FD86;    // CMenuManager::LoadSettings, thiscall: reads gta_vc.set
constexpr uintptr_t kWinMainLoad = 0x601906;     // mov ecx, FrontEndMenuManager; call LoadSettings
constexpr uintptr_t kSaveLimiter = 0x4902D1;     // SaveSettings: lea eax, [ebx + 0x25] (PrefsFrameLimiter), written
constexpr uintptr_t kDefaultLimiter = 0x49973F;  // Restore Defaults of Display Setup: ... PrefsFrameLimiter = 1

static uint8_t s_fileLimiter;  // the player's value, as gta_vc.set has it
static bool s_keepOff;         // set once saving writes the copy, so the file never gets OFF
static void(__thiscall* s_loadSettings)(void*);

// The game's start-up default or the loaded value moves to the copy. Runs after the game's own constructors, which
// set the default after VCUnlock installs when it is loaded as an ASI plugin.
static void __fastcall LoadSettings(void* menu, void*) {
    s_loadSettings(menu);
    s_fileLimiter = game::PrefsFrameLimiter();
    if (s_keepOff) game::PrefsFrameLimiter() = 0;
}

static void __cdecl OnSaveLimiter(fix::Regs* r, fix::Fpu*) { r->eax = (uint32_t)(uintptr_t)&s_fileLimiter; }

// Keeps the setting at OFF from now on: reading gta_vc.set and Restore Defaults no longer change it.
static void KeepGameLimiterOff() {
    PatchGroup g("menu: the game's own Frame Limiter setting");
    g.Expect(At(kWinMainLoad), "B9 [868638] E8 <48FD86>")
        .Expect(At(kSaveLimiter), "8D 43 25 83 C4 0C 6A 01 50 55 E8 <48DE20>")
        .Expect(At(kDefaultLimiter), "C6 45 24 01 C6 45 25 01");
    if (!g.Ok()) return;
    s_loadSettings = (decltype(s_loadSettings))At(kLoadSettings);
    s_fileLimiter = game::PrefsFrameLimiter();  // if the settings were loaded already
    if (!fix::RetargetCall(At(kWinMainLoad) + 5, At(kLoadSettings), (void*)LoadSettings, "menu: load Frame Limiter") ||
        !fix::Hook(At(kSaveLimiter) + 3, 6, "83 C4 0C 6A 01 50", OnSaveLimiter, "menu: save Frame Limiter"))
        return;
    const uint8_t off = 0;
    mem::Write(At(kDefaultLimiter) + 7, &off, 1);
    game::PrefsFrameLimiter() = 0;
    s_keepOff = true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

static bool Same(const Entry& e, const Entry& want) { return memcmp(&e, &want, sizeof e) == 0; }

void Install(bool world) {
    Entry* d = Entries(DisplaySetup);
    PatchGroup g("menu: Display Setup rows");
    // The Japanese DrawStandardMenus has a smaller stack frame, so its stack offsets differ.
    g.Expect(At(kDrawValue), "0F BF 86 [6D7B4A] 83 E8 07")
        .Expect(At(kDrawText) - 12, ver::Is(ver::JP) ? "B9 [94A228] E8 <584D60> 89 C3 80 BC 24 44 01 00 00 00"
                                                     : "B9 [94A228] E8 <584D60> 89 C3 80 BC 24 5C 01 00 00 00")
        .Expect(At(kEnter), "0F BF 9F [6D7B4A] 83 EB 02")
        .Expect(At(kEnterDone) - 7, "89 E9 E8 <498BED> 80 BC 24 80 00 00 00 00")
        .Expect(At(kChange), "0F BF 04 7D [6D7B4A] 83 E8 0F")
        .Expect(At(kChangeDone) - 22,
                "8A 84 24 84 00 00 00 89 E9 50 E8 <49892B> 89 E9 E8 <498BED> 83 BD F8 00 00 00 1E");
    if (!g.Ok()) return;
    // Without the fixed world the game keeps its own 30 fps limiter, and its own ON/OFF row for it.
    bool limiter = world && Same(d[2], kLimiterRow);
    if (world && !limiter) Log("[menu] the Frame Limiter row differs");
    bool fps = false;
    if (hud::FpsReady()) {
        PatchGroup f("menu: Show FPS row");
        f.Expect(At(game::CText_Get), "53 55 89 CB 83 EC 40");
        bool table = true;
        for (int i = 0; i < 3; ++i) table &= Same(d[9 + i], kDisplayOldTail[i]);
        if (!table) {
            Log("[menu] the rows below Frame Limiter differ");
            f.ok = false;
        }
        fps = f.Ok();
    }
    if (!limiter && !fps) return;

    s_textGet = (decltype(s_textGet))At(game::CText_Get);
    if (!fix::Hook(At(kDrawValue), 7, "0F BF 86 [6D7B4A]", OnDrawValue, "menu: row values") ||
        !fix::Hook(At(kEnter), 7, "0F BF 9F [6D7B4A]", OnEnter, "menu: Enter") ||
        !fix::Hook(At(kChange), 8, "0F BF 04 7D [6D7B4A]", OnChange, "menu: left/right"))
        return;
    s_limiter = limiter;
    if (s_limiter) KeepGameLimiterOff();
    if (uint8_t* tramp = fps ? fix::AllocCode(16) : nullptr) {
        memcpy(tramp, (void*)At(game::CText_Get), 7);
        tramp[7] = 0xE9;
        *(int32_t*)(tramp + 8) = (int32_t)(At(game::CText_Get) + 7 - (uintptr_t)(tramp + 12));
        s_textGet = (decltype(s_textGet))tramp;
        mem::Jmp(At(game::CText_Get), (void*)&TextGet, 7);
        mem::Write((uintptr_t)&d[9], kDisplayTail, sizeof kDisplayTail);
        s_fps = true;
    }
    Log("[menu] rows in place (frame limiter %d, FPS %d)", s_limiter, s_fps);
}

}  // namespace menu
