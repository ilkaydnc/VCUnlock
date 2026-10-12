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
//   ROLE      The settings as rows of Display Setup and Audio Setup
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The settings live in the game's own menus, drawn with the game's font and named in its five languages:
//
//   - Frame Limiter: VSYNC, 30, 60, 120, 144, 165, 240 or OFF.
//   - Wide Screen: aspect-correct HUD and the wider view.
//   - Screen Resolution: borderless, windowed or an exclusive mode at a chosen refresh rate.
//   - Show FPS (new row in Display Setup) and Mute In Background (new row in Audio Setup).
//
//   Four hooks do the work: the value switch of DrawStandardMenus draws our values, the Enter and left/right switches
//   of ProcessUserInput change them, and CText::Get names the two new rows. Changes apply at once and are written to
//   VCUnlock.ini; a new screen mode goes through the game's own device restart.
//
//   After that restart the game moves the cursor to row 5 and toggles it, which flipped Map Legend on every resolution
//   change. That toggle is skipped.
//
//   With a game loaded the restart would drop everything in video memory, so the game greys the row out. Borderless and
//   windowed share one video mode, so there the row stays open and switches only the window.
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

enum Screen { AudioSetup = 3, DisplaySetup = 4 };
// Actions of the rows taken over, and two numbers the game doesn't use for the new rows
enum Action : int16_t {
    FrameLimiter = 8,
    WideScreen = 11,
    ScreenRes = 43,
    RestoreDefaults = 47,
    ShowFps = 60,
    MuteInBackground = 61,
};

static Entry* Entries(int screen) { return (Entry*)(game::MenuScreens + screen * 0xE2 + 10); }

// Display Setup gains Show FPS above Restore Defaults; the page already uses 25-unit rows.
static const Entry kDisplayTail[3] = {
    {ShowFps, "FED_FPS", 0, DisplaySetup, 40, 303, 1, 0},
    {RestoreDefaults, "FET_DEF", 0, DisplaySetup, 320, 328, 3, 0},
    {34, "FEDS_TB", 0, 33, 320, 353, 3, 0},
};

// Audio Setup gains Mute In Background above Radio Station. The radio logos are drawn at a fixed height under that
// row, so the rows above it move from 29 to 25 units apart (Display Setup's spacing) to keep it where it was.
static const Entry kAudio[10] = {
    {13, "FEA_MUS", 0, AudioSetup, 40, 75, 1, 0},
    {14, "FEA_SFX", 0, AudioSetup, 40, 100, 1, 0},
    {53, "FEA_MPB", 0, AudioSetup, 40, 125, 1, 0},
    {44, "FEA_3DH", 0, AudioSetup, 40, 150, 1, 0},
    {45, "FEA_SPK", 0, AudioSetup, 40, 175, 1, 0},
    {49, "FET_DAM", 0, AudioSetup, 40, 200, 1, 0},
    {MuteInBackground, "FEA_MBG", 0, AudioSetup, 40, 225, 1, 0},
    {15, "FEA_RSS", 0, AudioSetup, 40, 250, 1, 0},
    {RestoreDefaults, "FET_DEF", 0, AudioSetup, 320, 367, 3, 0},
    {34, "FEDS_TB", 0, 33, 0, 0, 3, 0},
};

// ---------------------------------------------------------------------------------------------------------------------
//  TEXT
// ---------------------------------------------------------------------------------------------------------------------

// Written like the game's own menu text: capitals, French without accents.
struct Words {
    const char *showFps, *mute, *borderless, *windowed;
};

static const Words kWords[5] = {
    {"SHOW FPS", "MUTE IN BACKGROUND", "BORDERLESS", "WINDOWED"},
    {"AFFICHER LES FPS", "MUET EN ARRIERE-PLAN", "SANS BORDURE", "EN FENETRE"},
    {"FPS ANZEIGEN", "STUMM IM HINTERGRUND", "RAHMENLOS", "FENSTER"},
    {"MOSTRA FPS", "SILENZIA IN BACKGROUND", "SENZA BORDI", "IN FINESTRA"},
    {"MOSTRAR FPS", "SILENCIAR EN SEGUNDO PLANO", "SIN BORDES", "EN VENTANA"},
};

static const Words& Lang() {
    int l = game::PrefsLanguage;
    return kWords[l >= 0 && l < 5 ? l : 0];
}

static void Widen(uint16_t* out, size_t n, const char* s) {
    size_t i = 0;
    for (; s[i] && i + 1 < n; ++i) out[i] = (uint8_t)s[i];
    out[i] = 0;
}

static const uint16_t*(__thiscall* s_textGet)(void*, const char*);

static const uint16_t* GameText(const char* key) { return s_textGet((void*)game::TheText, key); }

// CText::Get: the labels of the two new rows.
static const uint16_t* __fastcall TextGet(void* text, void*, const char* key) {
    if (key[0] == 'F' && key[1] == 'E' && key[3] == '_') {
        static uint16_t buf[2][40];
        if (!strcmp(key, "FED_FPS")) {
            Widen(buf[0], 40, Lang().showFps);
            return buf[0];
        }
        if (!strcmp(key, "FEA_MBG")) {
            Widen(buf[1], 40, Lang().mute);
            return buf[1];
        }
    }
    return s_textGet(text, key);
}

// ---------------------------------------------------------------------------------------------------------------------
//  SCREEN RESOLUTION
// ---------------------------------------------------------------------------------------------------------------------

// The list is rebuilt when the row comes back into view, so it shows the mode the game runs in.
static const window::Choice* s_choices;
static int s_numChoices, s_sel = -1;
static ULONGLONG s_seen;

static void RefreshChoices() {
    if (s_sel < 0 || GetTickCount64() - s_seen > 300) {
        s_numChoices = window::Choices(&s_choices);
        s_sel = window::CurrentChoice();
        if (s_sel < 0 && s_numChoices) s_sel = 0;
    }
    s_seen = GetTickCount64();
}

static const uint16_t* ResolutionText() {
    RefreshChoices();
    static uint16_t wide[64];
    if (s_sel < 0) return nullptr;
    const window::Choice& c = s_choices[s_sel];
    char s[64];
    if (c.mode == 1) sprintf_s(s, "%d X %d %s", c.w, c.h, Lang().borderless);
    else if (c.mode == 2) sprintf_s(s, "%d X %d %s", c.w, c.h, Lang().windowed);
    else if (c.hz > 0) sprintf_s(s, "%d X %d %dHZ", c.w, c.h, c.hz);
    else sprintf_s(s, "%d X %d", c.w, c.h);
    Widen(wide, 64, s);
    return wide;
}

// As in the original, a new screen mode needs the main menu: the device restart drops everything a loaded game has
// in video memory. Borderless and windowed share one mode, so with a game loaded the row switches between those two.
static bool ResolutionOpen() { return game::MenuGameNotLoaded || window::Mode() != 0; }
static bool Allowed(const window::Choice& c) { return game::MenuGameNotLoaded || (window::Mode() != 0 && c.mode != 0); }

// Enter on the row. True: the game's own handler (0x4998DF) restarts the device in the chosen mode.
static bool ApplyResolution() {
    RefreshChoices();
    if (s_sel < 0 || s_sel == window::CurrentChoice()) return false;
    const window::Choice& c = s_choices[s_sel];
    if (!game::MenuGameNotLoaded) {
        window::SwitchWindowed(c);
        return false;
    }
    window::PrepareChoice(c);
    game::MenuDisplayVideoMode = c.vm;
    game::MenuPrefsVideoMode = -1;  // differs, so the handler calls psSelectScreenVM
    s_sel = -1;                     // rebuilt once the new device runs
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  VALUES
// ---------------------------------------------------------------------------------------------------------------------

static bool s_limiter, s_wide, s_res, s_fps;

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

static const uint16_t* OnOff(int on) { return GameText(on ? "FEM_ON" : "FEM_OFF"); }

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
        case WideScreen: return s_wide ? OnOff(g_cfg.widescreen) : nullptr;
        case ScreenRes: return s_res ? ResolutionText() : nullptr;
        case ShowFps: return OnOff(g_cfg.showFps);
        case MuteInBackground: return OnOff(g_cfg.muteOnFocusLoss);
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
        case WideScreen:
            if (!s_wide) return false;
            g_cfg.widescreen = !g_cfg.widescreen;
            hud::ApplyWidescreen();
            break;
        case ScreenRes:
            if (!s_res) return false;
            if (ResolutionOpen()) {
                RefreshChoices();
                for (int k = 0; k < s_numChoices; ++k) {  // the next choice that can be applied now
                    s_sel = (s_sel + (dir > 0 ? 1 : s_numChoices - 1)) % s_numChoices;
                    if (Allowed(s_choices[s_sel])) break;
                }
            }
            return true;
        case ShowFps: g_cfg.showFps = !g_cfg.showFps; break;
        case MuteInBackground: g_cfg.muteOnFocusLoss = !g_cfg.muteOnFocusLoss; break;
        default: return false;
    }
    SaveConfig();
    return true;
}

static void RestoreOurDefaults(int screen) {
    Config def;
    if (screen == DisplaySetup) {
        if (s_limiter) {
            g_cfg.vsync = def.vsync;
            g_cfg.fpsLimit = def.fpsLimit;
            render::ApplyVsync();
        }
        if (s_wide) {
            g_cfg.widescreen = def.widescreen;
            hud::ApplyWidescreen();
        }
        g_cfg.showFps = def.showFps;
    } else if (screen == AudioSetup) g_cfg.muteOnFocusLoss = def.muteOnFocusLoss;
    else return;
    SaveConfig();
}

// ---------------------------------------------------------------------------------------------------------------------
//  HOOKS
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t kDrawValue = 0x49EA02;  // movsx eax, [esi + entries]: the value switch of DrawStandardMenus
constexpr uintptr_t kDrawText = 0x49EDF4;   // ebx = value text, draws it
constexpr uintptr_t kDrawDimmed = 0x49EC81;  // Screen Resolution while a game is loaded: dimmed colour, then kDrawText
constexpr uintptr_t kEnter = 0x49956D;      // movsx ebx, [edi + entries]: the Enter switch of ProcessUserInput
constexpr uintptr_t kEnterDone = 0x499BDA;  // after the switch and ProcessOnOffMenuOptions
constexpr uintptr_t kChange = 0x499CAA;     // movsx eax, [edi*2 + entries]: the left/right switch
constexpr uintptr_t kChangeDone = 0x499F03;  // after CheckSliderMovement and ProcessOnOffMenuOptions
constexpr uintptr_t kHelpNotAvailable = 0x48F7D3;  // Screen Resolution's helper text with a game loaded: FEH_NA
constexpr uintptr_t kHelpAdjust = 0x48F7E4;        // the same before a game is loaded: the left/right hint
constexpr uintptr_t kOnOffCall = 0x499BD5;   // call ProcessOnOffMenuOptions after the Enter switch
constexpr uintptr_t kProcessOnOff = 0x498BED;  // CMenuManager::ProcessOnOffMenuOptions, thiscall

static bool s_resApplied;

// The game's Screen Resolution handler leaves the cursor on row 5 (Map Legend), which this call would then toggle.
static void __fastcall OnOffAfterEnter(void* menu, void*) {
    if (s_resApplied) {
        s_resApplied = false;
        return;
    }
    ((void(__fastcall*)(void*, void*))kProcessOnOff)(menu, nullptr);
}

static void __cdecl OnDrawValue(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->esi + game::MenuScreens + 10);
    const uint16_t* text = ValueText(action);
    if (!text) return;
    r->ebx = (uint32_t)(uintptr_t)text;
    r->cont = action == ScreenRes && !ResolutionOpen() ? kDrawDimmed : kDrawText;
}

static void __cdecl OnEnter(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->edi + game::MenuScreens + 10);
    s_resApplied = false;
    if (action == RestoreDefaults) RestoreOurDefaults(game::MenuCurrScreen);  // and the game's own defaults
    else if (action == ScreenRes && s_res) {
        if (ApplyResolution()) s_resApplied = true;
        else r->cont = kEnterDone;
    } else if (Change(action, 1)) r->cont = kEnterDone;
}

static void __cdecl OnChange(fix::Regs* r, fix::Fpu*) {
    int action = *(int16_t*)(r->edi * 2 + game::MenuScreens + 10);
    int8_t dir = *((int8_t*)r->Stack() + 0x84);  // changeAmount
    if (Change(action, dir)) r->cont = kChangeDone;
}

static void __cdecl OnResolutionHelp(fix::Regs* r, fix::Fpu*) {
    if (ResolutionOpen()) r->cont = kHelpAdjust;
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

static bool Same(const Entry& e, const Entry& want) { return memcmp(&e, &want, sizeof e) == 0; }

void Install(bool world) {
    PatchGroup g("menu: Display Setup and Audio Setup rows");
    g.Expect(kDrawValue, "0F BF 86 4A 7B 6D 00 83 E8 07")
        .Expect(kDrawText - 12, "B9 28 A2 94 00 E8 6E 5F 0E 00 89 C3 80 BC 24 5C 01 00 00 00")
        .Expect(kDrawDimmed - 19, "80 7D 6C 00 59 8D 9C 24 94 00 00 00 59 0F 85 73 01 00 00 8B 85 08 01 00 00")
        .Expect(kEnter, "0F BF 9F 4A 7B 6D 00 83 EB 02")
        .Expect(kEnterDone - 7, "89 E9 E8 13 F0 FF FF 80 BC 24 80 00 00 00 00")
        .Expect(kChange, "0F BF 04 7D 4A 7B 6D 00 83 E8 0F")
        .Expect(kChangeDone - 22,
                "8A 84 24 84 00 00 00 89 E9 50 E8 2F EA FF FF 89 E9 E8 EA EC FF FF 83 BD F8 00 00 00 1E")
        // Screen Resolution's Enter case: m_nPrefsVideoMode = m_nDisplayVideoMode, psSelectScreenVM
        .Expect(0x4998DF, "8B 45 60 3B 45 5C 0F 84 E8 02 00 00 89 45 5C FF 75 5C E8 EA 7A 16 00")
        .Expect(game::CText_Get, "53 55 89 CB 83 EC 40");
    // The table as the game has it: Display Setup rows 2, 4, 8-11 and Audio Setup rows 0-11
    Entry* d = Entries(DisplaySetup);
    Entry* a = Entries(AudioSetup);
    static const Entry kDisplayOld[6] = {
        {FrameLimiter, "FEM_FRM", 0, DisplaySetup, 40, 128, 1, 0},
        {WideScreen, "FED_WIS", 0, DisplaySetup, 40, 178, 1, 0},
        {ScreenRes, "FED_RES", 0, DisplaySetup, 40, 278, 1, 0},
        {RestoreDefaults, "FET_DEF", 0, DisplaySetup, 320, 303, 3, 0},
        {34, "FEDS_TB", 0, 33, 320, 328, 3, 0},
        {},
    };
    static const Entry kAudioOld[12] = {
        {13, "FEA_MUS", 0, AudioSetup, 40, 76, 1, 0},  {14, "FEA_SFX", 0, AudioSetup, 0, 0, 1, 0},
        {53, "FEA_MPB", 0, AudioSetup, 0, 0, 1, 0},    {44, "FEA_3DH", 0, AudioSetup, 0, 0, 1, 0},
        {45, "FEA_SPK", 0, AudioSetup, 0, 0, 1, 0},    {49, "FET_DAM", 0, AudioSetup, 0, 0, 1, 0},
        {15, "FEA_RSS", 0, AudioSetup, 0, 0, 1, 0},    {RestoreDefaults, "FET_DEF", 0, AudioSetup, 320, 367, 3, 0},
        {34, "FEDS_TB", 0, 33, 0, 0, 3, 0},            {}, {}, {},
    };
    static const int kDisplayRows[6] = {2, 4, 8, 9, 10, 11};
    bool table = true;
    for (int i = 0; i < 6; ++i) table &= Same(d[kDisplayRows[i]], kDisplayOld[i]);
    for (int i = 0; i < 12; ++i) table &= Same(a[i], kAudioOld[i]);
    if (!table) {
        Log("[menu] menu table differs");
        g.ok = false;
    }
    if (!g.Ok()) return;

    uint8_t* tramp = fix::AllocCode(16);
    if (!tramp) return;
    memcpy(tramp, (void*)game::CText_Get, 7);
    tramp[7] = 0xE9;
    *(int32_t*)(tramp + 8) = (int32_t)(game::CText_Get + 7 - (uintptr_t)(tramp + 12));
    s_textGet = (decltype(s_textGet))tramp;

    s_limiter = world;  // without the fixed world the game keeps its own 30 fps limiter
    s_wide = hud::WidescreenReady();
    s_res = true;
    s_fps = hud::FpsReady();
    if (!fix::Hook(kDrawValue, 7, "0F BF 86 4A 7B 6D 00", OnDrawValue, "menu: row values") ||
        !fix::Hook(kEnter, 7, "0F BF 9F 4A 7B 6D 00", OnEnter, "menu: Enter") ||
        !fix::Hook(kChange, 8, "0F BF 04 7D 4A 7B 6D 00", OnChange, "menu: left/right"))
        return;
    fix::RetargetCall(kOnOffCall, kProcessOnOff, (void*)OnOffAfterEnter, "menu: Map Legend after a resolution change");
    // TheText.Get("FEH_NA") ... jmp; kHelpAdjust: TheText.Get(left/right hint)
    fix::Hook(kHelpNotAvailable, 5, "B9 28 A2 94 00 68 8C B3 68 00 E8 7E 55 0F 00 EB 0F B9 28 A2 94 00 68 94 B3 68 00",
              OnResolutionHelp, "menu: Screen Resolution hint");
    mem::Jmp(game::CText_Get, (void*)&TextGet, 7);
    if (s_fps) mem::Write((uintptr_t)&d[9], kDisplayTail, sizeof kDisplayTail);
    mem::Write((uintptr_t)a, kAudio, sizeof kAudio);
    Log("[menu] rows in place (frame limiter %d, widescreen %d, FPS %d)", s_limiter, s_wide, s_fps);
}

}  // namespace menu
