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
//   FILE      window.cpp
//   MODULE    window
//   ROLE      Window modes, focus, cursor and system-settings fixes
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   - Exclusive fullscreen, borderless fullscreen window or a resizable window.
//   - Focus loss can pause the game and mute the audio; the cursor is confined while playing.
//   - Accessibility shortcuts and screen savers stay as the user set them.
//   - A stale 3D audio provider index no longer crashes the Audio Setup page after a focus change.
//   - The device and video-mode selection is replaced, DPI awareness is declared, the intro movies can be skipped and
//     the mouse is no longer captured exclusively.
//   - Screen Resolution lists borderless, windowed and every 32-bit fullscreen mode at each refresh rate. A change goes
//     through the game's own device restart, and SelectDevice gives the window the new style. Borderless <-> windowed
//     with a game loaded changes only the window, from the message loop.
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
#include "render.h"
#include "modules.h"
#include "d3d8min.h"
#include <cstdio>

namespace window {

// ---------------------------------------------------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------------------------------------------------

enum Mode { Exclusive = 0, Borderless = 1, Windowed = 2 };
static HWND s_hwnd;
static WNDPROC s_origProc;
static HWND(WINAPI* s_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                       LPVOID);
static BOOL(WINAPI* s_SystemParametersInfoA)(UINT, UINT, PVOID, UINT);
static bool s_keepAwake;
static int s_mode;                // effective mode (may fall back to exclusive)
static int s_clientW, s_clientH;  // client size chosen for borderless/windowed
static bool s_active = true, s_sizing, s_muted;
static RECT s_clip;
static bool s_clipped;

int Mode() { return s_mode; }

static RECT PrimaryMonitor(bool work) {
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoA(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return work ? mi.rcWork : mi.rcMonitor;
}

// ---------------------------------------------------------------------------------------------------------------------
//  CURSOR CONFINEMENT
// ---------------------------------------------------------------------------------------------------------------------

static void Unclip() {
    if (s_clipped) {
        ClipCursor(nullptr);
        s_clipped = false;
    }
}

static void UpdateClip() {
    bool want = s_hwnd && s_active && !s_sizing && !IsIconic(s_hwnd) && GetForegroundWindow() == s_hwnd;
    // windowed: free the cursor in menus so the title bar and other windows stay reachable
    if (want && s_mode == Windowed && (game::MenuActive || game::GameState != 9)) want = false;
    if (!want) {
        Unclip();
        return;
    }
    RECT r;
    GetClientRect(s_hwnd, &r);
    POINT a{r.left, r.top}, b{r.right, r.bottom};
    ClientToScreen(s_hwnd, &a);
    ClientToScreen(s_hwnd, &b);
    RECT c{a.x, a.y, b.x, b.y}, cur;
    GetClipCursor(&cur);
    if (!s_clipped || !EqualRect(&cur, &c)) {
        ClipCursor(&c);
        s_clip = c;
        s_clipped = true;
    }
}

// Called once per drawn frame from the timer.
void Tick() {
    if (s_hwnd) UpdateClip();
}

// Frame cap while the window is in the background (0 = none).
int BackgroundFps() {
    if (s_active || !g_cfg.pauseOnFocusLoss) return 0;
    return 15;
}

// ---------------------------------------------------------------------------------------------------------------------
//  FOCUS
// ---------------------------------------------------------------------------------------------------------------------

static void OnFocusLost() {
    s_active = false;
    Unclip();
    if (g_cfg.pauseOnFocusLoss && game::GameState == 9 && !game::MenuGameNotLoaded && !game::MenuActive)
        game::MenuStartUpRequested = 1;  // same request the game makes in CGame::InitAfterFocusLoss
    // exclusive mode: RW's device-lost path already releases/reacquires the audio device
    if (g_cfg.muteOnFocusLoss && s_mode != Exclusive && !s_muted) {
        ((void(__thiscall*)(void*))game::DMAudio_ReleaseDigitalHandle)((void*)game::DMAudio);
        s_muted = true;
    }
}

static void OnFocusGained() {
    s_active = true;
    if (s_muted) {
        ((void(__thiscall*)(void*))game::DMAudio_ReacquireDigitalHandle)((void*)game::DMAudio);
        s_muted = false;
    }
    UpdateClip();
}

// ---------------------------------------------------------------------------------------------------------------------
//  WINDOW PROCEDURE
// ---------------------------------------------------------------------------------------------------------------------

constexpr UINT kSwitchWindowMsg = WM_APP + 0x56;  // posted by SwitchWindowed
static void __cdecl ApplyWindowMode();

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case kSwitchWindowMsg: ApplyWindowMode(); return 0;
        case WM_ACTIVATEAPP:
            Log("WM_ACTIVATEAPP %d", (int)wp);
            if (wp) OnFocusGained();
            else OnFocusLost();
            break;
        case WM_SETCURSOR:
            // original: ShowCursor(FALSE) on every message (counter drift, hidden over the title bar too)
            if (LOWORD(lp) == HTCLIENT && s_active) {
                SetCursor(nullptr);
                return TRUE;
            }
            return DefWindowProcA(h, msg, wp, lp);
        case WM_SETFOCUS:
            // original: CGame::InitAfterFocusLoss (0x4A4EA0) re-selects the 3D audio provider and opens the pause menu
            // when focus comes back; with PauseOnFocusLoss=0 the game keeps running, so drop that menu request
            if (!g_cfg.pauseOnFocusLoss) {
                uint8_t before = game::MenuStartUpRequested;
                LRESULT r = CallWindowProcA(s_origProc, h, msg, wp, lp);
                game::MenuStartUpRequested = before;
                return r;
            }
            break;
        case WM_SYSKEYDOWN:
            if (wp == VK_F4) return DefWindowProcA(h, msg, wp, lp);  // Alt+F4 -> WM_CLOSE (game returned 0)
            break;
        case WM_SIZING: return DefWindowProcA(h, msg, wp, lp);  // original ran a whole game frame here
        case WM_ENTERSIZEMOVE:
            s_sizing = true;
            Unclip();
            break;
        case WM_EXITSIZEMOVE:
            s_sizing = false;
            UpdateClip();
            if (s_mode == Windowed && !IsZoomed(h) && s_clientW > 0 &&
                (g_cfg.width != s_clientW || g_cfg.height != s_clientH)) {
                g_cfg.width = s_clientW;
                g_cfg.height = s_clientH;
                SaveConfig();
            }
            break;
        case WM_GETMINMAXINFO:
            if (s_mode == Windowed) {
                ((MINMAXINFO*)lp)->ptMinTrackSize = POINT{320, 240};
                return 0;
            }
            break;
        case WM_SIZE: {
            LRESULT r = CallWindowProcA(s_origProc, h, msg, wp, lp);
            int w = LOWORD(lp), hh = HIWORD(lp);
            if (s_mode != Exclusive && w > 0 && hh > 0 && wp != SIZE_MINIMIZED) {
                game::RsGlobalMaxWidth = w;
                game::RsGlobalMaxHeight = hh;  // keeps menu mouse centring right
                if (s_mode == Windowed && wp == SIZE_RESTORED) {
                    s_clientW = w;
                    s_clientH = hh;
                }
            }
            UpdateClip();
            return r;
        }
        case WM_MOVE: UpdateClip(); break;
        case WM_DESTROY: Unclip(); break;
    }
    return CallWindowProcA(s_origProc, h, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------------------------------
//  IMPORT HOOKS
// ---------------------------------------------------------------------------------------------------------------------

// The game window is subclassed the moment it exists.
static HWND WINAPI Hk_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int hgt,
                                      HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    HWND h = s_CreateWindowExA(ex, cls, title, style, x, y, w, hgt, parent, menu, inst, param);
    if (h && !s_hwnd && !IS_INTRESOURCE(cls) && _stricmp(cls, "Grand theft auto 3") == 0) {
        s_hwnd = h;
        s_origProc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)&WndProc);
        Log("game window %p created, subclassed", (void*)h);
    }
    return h;
}

// The game sets the foreground-lock timeout to 0 (system-wide, never restored) and disables the
// screensaver/power saving, then force-enables them on exit. Replace with per-process equivalents.
static BOOL WINAPI Hk_SystemParametersInfoA(UINT action, UINT ui, PVOID pv, UINT flags) {
    switch (action) {
        case SPI_SETFOREGROUNDLOCKTIMEOUT:
            Log("SPI_SETFOREGROUNDLOCKTIMEOUT(%u) blocked", (unsigned)(uintptr_t)pv);
            return TRUE;
        case SPI_SETSCREENSAVEACTIVE:
        case SPI_SETLOWPOWERACTIVE:
        case SPI_SETPOWEROFFACTIVE:
            if (!s_keepAwake) {
                s_keepAwake = true;
                SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
            }
            return TRUE;
    }
    return s_SystemParametersInfoA(action, ui, pv, flags);
}

// ---------------------------------------------------------------------------------------------------------------------
//  WINDOW STYLE
// ---------------------------------------------------------------------------------------------------------------------

// A new style keeps WS_VISIBLE: after a Screen Resolution change in the menu the window is already shown.
static void SetStyle(HWND hwnd, DWORD style) {
    SetWindowLongA(hwnd, GWL_STYLE, style | (GetWindowLongA(hwnd, GWL_STYLE) & WS_VISIBLE));
}

// Replaces WinMain's SetWindowLongA(WS_POPUP) + SetWindowPos.
static void SetExclusiveStyle(HWND hwnd) {
    SetStyle(hwnd, WS_POPUP);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

// Client size of the window in windowed mode: Width x Height if the window fits the work area, else 75% of the desktop.
static void WindowedSize(int& w, int& h) {
    RECT wa = PrimaryMonitor(true), m = PrimaryMonitor(false);
    w = g_cfg.width;
    h = g_cfg.height;
    RECT r{0, 0, w, h};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    if (w <= 0 || h <= 0 || r.right - r.left > wa.right - wa.left || r.bottom - r.top > wa.bottom - wa.top) {
        w = (m.right - m.left) * 3 / 4;
        h = (m.bottom - m.top) * 3 / 4;
    }
}

static void __cdecl ApplyWindowMode() {
    HWND hwnd = *(HWND*)game::RsGlobalPs;
    // HWND_NOTOPMOST: a released exclusive fullscreen device can leave the window topmost
    if (s_mode == Borderless) {
        RECT m = PrimaryMonitor(false);
        s_clientW = m.right - m.left;
        s_clientH = m.bottom - m.top;
        SetStyle(hwnd, WS_POPUP);
        SetWindowPos(hwnd, HWND_NOTOPMOST, m.left, m.top, s_clientW, s_clientH, SWP_FRAMECHANGED);
    } else if (s_mode == Windowed) {
        RECT wa = PrimaryMonitor(true);
        int w, h;
        WindowedSize(w, h);
        DWORD style = WS_OVERLAPPEDWINDOW;
        RECT r{0, 0, w, h};
        AdjustWindowRectEx(&r, style, FALSE, 0);
        int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = wa.left + ((wa.right - wa.left) - ww) / 2, y = wa.top + ((wa.bottom - wa.top) - wh) / 2;
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
        SetStyle(hwnd, style);
        SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, ww, wh, SWP_FRAMECHANGED);
        RECT c;
        GetClientRect(hwnd, &c);
        s_clientW = c.right;
        s_clientH = c.bottom;
    } else {
        SetExclusiveStyle(hwnd);
    }
    Log("window mode %d applied (client %dx%d)", s_mode, s_clientW, s_clientH);
}

// ---------------------------------------------------------------------------------------------------------------------
//  PSSELECTDEVICE REPLACEMENT
// ---------------------------------------------------------------------------------------------------------------------

using RwVideoMode = game::RwVideoMode;
static int(__cdecl* const RwGetNumSubSystems)() = (int(__cdecl*)())game::RwEngineGetNumSubSystems;
static void*(__cdecl* const RwGetSubSystemInfo)(void*, int) =
    (void*(__cdecl*)(void*, int))game::RwEngineGetSubSystemInfo;
static int(__cdecl* const RwGetCurrentSubSystem)() = (int(__cdecl*)())game::RwEngineGetCurrentSubSystem;
static int(__cdecl* const RwSetSubSystem)(int) = (int(__cdecl*)(int))game::RwEngineSetSubSystem;
static int(__cdecl* const RwGetNumVideoModes)() = (int(__cdecl*)())game::RwEngineGetNumVideoModes;
static RwVideoMode*(__cdecl* const RwGetVideoModeInfo)(RwVideoMode*, int) =
    (RwVideoMode*(__cdecl*)(RwVideoMode*, int))game::RwEngineGetVideoModeInfo;
static int(__cdecl* const RwSetVideoMode)(int) = (int(__cdecl*)(int))game::RwEngineSetVideoMode;
static int(__cdecl* const RwSetRefreshRate)(int) = (int(__cdecl*)(int))game::RwD3D8EngineSetRefreshRate;
static char**(__cdecl* const GetVideoModeList)() = (char**(__cdecl*)())game::psGetVideoModeList;

static int FindExclusive(int w, int h) {
    int n = RwGetNumVideoModes(), best = -1, bestArea = 0;
    for (int i = 0; i < n; ++i) {
        RwVideoMode vm;
        RwGetVideoModeInfo(&vm, i);
        if (!(vm.flags & 1) || vm.depth != 32) continue;
        if (vm.width == w && vm.height == h) return i;
        if (vm.width * vm.height > bestArea) {
            bestArea = vm.width * vm.height;
            best = i;
        }
    }
    return best;
}

static int __cdecl SelectDevice() {
    if (!game::UseDefaultVM) {
        int n = RwGetNumSubSystems();
        if (!n) return 0;
        game::GnumSubSystems = n > 16 ? 16 : n;
        for (int i = 0; i < game::GnumSubSystems; ++i) RwGetSubSystemInfo((void*)(game::GsubSysInfo + i * 0x50), i);
        game::GcurSel = RwGetCurrentSubSystem();
    }
    if (!RwSetSubSystem(game::GcurSel)) return 0;

    if (!game::UseDefaultVM) {
        int idx = -1;
        if (s_mode != Exclusive) {
            RwVideoMode vm;
            RwGetVideoModeInfo(&vm, 0);
            if (RwGetNumVideoModes() > 0 && !(vm.flags & 1)) idx = 0;
            else {
                Log("no windowed video mode available, falling back to exclusive fullscreen");
                s_mode = Exclusive;
                SetExclusiveStyle(*(HWND*)game::RsGlobalPs);
            }
        }
        if (s_mode == Exclusive) {
            RECT m = PrimaryMonitor(false);
            int w = g_cfg.width > 0 ? g_cfg.width : m.right - m.left;
            int h = g_cfg.height > 0 ? g_cfg.height : m.bottom - m.top;
            idx = FindExclusive(w, h);
            if (idx < 0) idx = FindExclusive(m.right - m.left, m.bottom - m.top);
            if (idx < 0) {
                MessageBoxA(nullptr, "No 32-bit fullscreen video mode found.", "GTA: Vice City (VCUnlock)",
                            MB_ICONERROR);
                return 0;
            }
        }
        game::GcurSelVM = idx;
    }

    RwVideoMode vm;
    RwGetVideoModeInfo(&vm, game::GcurSelVM);
    if (game::UseDefaultVM) {
        // Screen Resolution changed in the menu (psSelectScreenVM): the old device is gone and the new one is created
        // after this returns, so the window takes the style and size of the new mode now
        HWND hwnd = *(HWND*)game::RsGlobalPs;
        if (vm.flags & 1) {
            s_mode = Exclusive;
            SetExclusiveStyle(hwnd);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, vm.width, vm.height, SWP_NOACTIVATE);
        } else {
            if (s_mode == Exclusive) s_mode = Borderless;
            ApplyWindowMode();
        }
    }
    game::MenuCurrOption = 0;
    if (!RwSetVideoMode(game::GcurSelVM)) return 0;
    uint8_t* ps = (uint8_t*)game::RsGlobalPs;
    if (vm.flags & 1) {
        if (g_cfg.refreshRate > 0) RwSetRefreshRate(g_cfg.refreshRate);  // otherwise RW uses the mode's max refresh
        game::RsGlobalMaxWidth = game::RsGlobalWidth = vm.width;
        game::RsGlobalMaxHeight = game::RsGlobalHeight = vm.height;
        *(int32_t*)(ps + 8) = 1;
    } else {
        game::RsGlobalMaxWidth = game::RsGlobalWidth = s_clientW;
        game::RsGlobalMaxHeight = game::RsGlobalHeight = s_clientH;
        *(int32_t*)(ps + 8) = 0;
        // the menu's resolution list has no entry for the windowed mode (index 0): give it a label,
        // otherwise drawing the Display Setup page dereferences NULL
        char** list = GetVideoModeList();
        if (list && !list[0]) {
            auto calloc_ = *(void*(__cdecl**)(size_t, size_t))((uint8_t*)game::RwEngineInstance + 0x13C);
            if (char* s = (char*)calloc_(100, 1)) {
                sprintf_s(s, 100, "%d X %d %s", s_clientW, s_clientH, s_mode == Borderless ? "BORDERLESS" : "WINDOWED");
                list[0] = s;
            }
        }
    }
    Log("video mode %d: %dx%dx%d %s", game::GcurSelVM, vm.width, vm.height, vm.depth,
        (vm.flags & 1) ? "exclusive" : "windowed");
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------------
//  SCREEN RESOLUTION CHOICES
// ---------------------------------------------------------------------------------------------------------------------

// Display Setup's Screen Resolution row: borderless, windowed, then every 32-bit exclusive mode at each refresh rate.
static Choice s_choices[512];
static int s_numChoices;

static void AddChoice(int mode, int w, int h, int hz, int vm) {
    if (s_numChoices < (int)(sizeof s_choices / sizeof s_choices[0])) s_choices[s_numChoices++] = {mode, w, h, hz, vm};
}

int Choices(const Choice** list) {
    s_numChoices = 0;
    int n = RwGetNumVideoModes();
    RwVideoMode vm;
    if (n > 0 && RwGetVideoModeInfo(&vm, 0) && !(vm.flags & 1)) {
        RECT m = PrimaryMonitor(false);
        int w, h;
        if (s_mode == Windowed && s_clientW > 0) {
            w = s_clientW;
            h = s_clientH;
        } else WindowedSize(w, h);
        AddChoice(Borderless, m.right - m.left, m.bottom - m.top, 0, 0);
        AddChoice(Windowed, w, h, 0, 0);
    }
    for (int i = 0; i < n; ++i) {
        RwGetVideoModeInfo(&vm, i);
        if (!(vm.flags & 1) || vm.depth != 32) continue;
        bool dup = false;
        for (int k = 0; k < s_numChoices && !dup; ++k)
            dup = s_choices[k].mode == Exclusive && s_choices[k].w == vm.width && s_choices[k].h == vm.height;
        if (dup) continue;
        int rates[32];
        int r = render::RefreshRates(vm.width, vm.height, rates, 32);
        if (!r) AddChoice(Exclusive, vm.width, vm.height, 0, i);
        for (int k = 0; k < r; ++k) AddChoice(Exclusive, vm.width, vm.height, rates[k], i);
    }
    *list = s_choices;
    return s_numChoices;
}

// Index of the mode the game runs in, in the last Choices list.
int CurrentChoice() {
    int any = -1;
    for (int i = 0; i < s_numChoices; ++i) {
        const Choice& c = s_choices[i];
        if (c.mode != s_mode) continue;
        if (c.mode != Exclusive) return i;
        if (c.vm != game::GcurSelVM) continue;
        if (c.hz == render::g_refresh) return i;
        if (any < 0) any = i;
    }
    return any;
}

// Takes the settings of a choice. The game's own Screen Resolution handler then restarts the device through
// psSelectScreenVM, and SelectDevice gives the window its new style.
void PrepareChoice(const Choice& c) {
    s_mode = c.mode;
    g_cfg.windowMode = c.mode;
    if (c.mode != Borderless) {
        g_cfg.width = c.w;
        g_cfg.height = c.h;
    }
    if (c.mode == Exclusive) g_cfg.refreshRate = c.hz;
    SaveConfig();
    Log("screen resolution: mode %d %dx%d %d Hz (video mode %d)", c.mode, c.w, c.h, c.hz, c.vm);
}

// Borderless <-> windowed while a game is loaded. Both run on RW's windowed video mode, so the device stays and only
// the window changes. It changes from the message loop, as when the user resizes it: the game's own WM_SIZE handler
// then resizes the camera.
bool SwitchWindowed(const Choice& c) {
    if (s_mode == Exclusive || c.mode == Exclusive || c.mode == s_mode || !s_hwnd) return false;
    s_mode = c.mode;
    g_cfg.windowMode = c.mode;
    SaveConfig();
    PostMessageA(s_hwnd, kSwitchWindowMsg, 0, 0);
    Log("screen resolution: window mode %d while a game is loaded", c.mode);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  DPI AWARENESS
// ---------------------------------------------------------------------------------------------------------------------

// The game is DPI-unaware: on scaled desktops Windows reports a virtualized desktop size and
// bitmap-stretches the window. Declare per-monitor awareness before any window exists.
static void SetDpiAware() {
    HMODULE u = GetModuleHandleA("user32.dll");
    using Ctx = BOOL(WINAPI*)(HANDLE);
    if (auto f = (Ctx)GetProcAddress(u, "SetProcessDpiAwarenessContext")) {
        if (f((HANDLE)-4 /*PER_MONITOR_AWARE_V2*/) || f((HANDLE)-3 /*PER_MONITOR_AWARE*/)) {
            Log("DPI awareness: per-monitor");
            return;
        }
    }
    if (auto f = (BOOL(WINAPI*)())GetProcAddress(u, "SetProcessDPIAware")) {
        f();
        Log("DPI awareness: system");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  AUDIO SETUP
// ---------------------------------------------------------------------------------------------------------------------

// Every WM_SETFOCUS runs CGame::InitAfterFocusLoss, which copies m_lastWorking3DAudioProvider into the menu's provider
// index. LoadSettings fills that field with the raw saved value: 0xFE (auto) or an index the current audio devices may
// not have. The Audio Setup page then gets NULL from Get3DProviderName and AsciiToUnicode crashes. Borderless and
// windowed get focus back far more often than exclusive fullscreen. An empty name is drawn instead, and the page
// switches the index to the provider in use while its row is not selected.
static const char* __fastcall Get3DProviderName(void* dmAudio, void*, int index) {
    auto get = (const char*(__thiscall*)(void*, int))game::DMAudio_Get3DProviderName;
    const char* name = get(dmAudio, index);
    return name ? name : "";
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void InstallEarly(HMODULE exe) {
    SetDpiAware();
    s_mode = g_cfg.windowMode;
    s_CreateWindowExA = (decltype(s_CreateWindowExA))mem::HookImport(exe, "USER32.dll", "CreateWindowExA",
                                                                     (void*)&Hk_CreateWindowExA);
    s_SystemParametersInfoA = (decltype(s_SystemParametersInfoA))mem::HookImport(
        exe, "USER32.dll", "SystemParametersInfoA", (void*)&Hk_SystemParametersInfoA);
}

// WinMain state 3 starts the intro movie without checking bNoMovies (the logo movie path does check).
static void __cdecl PlayIntroMovie(int cmdShow, const char* file) {
    if (*(uint32_t*)0x973C04) return;
    ((void(__cdecl*)(int, const char*))0x600430)(cmdShow, file);
}

void Install() {
    // In borderless/windowed the DirectShow movie renderer creates a D3D9 device and the Steam overlay's
    // Direct3DCreate9 hook recurses until the stack overflows -> movies are always skipped there.
    bool skipMovies = g_cfg.skipIntro || s_mode != Exclusive;
    if (skipMovies) {
        PatchGroup g("window: skip intro movies");
        // WM_ACTIVATEAPP sets bNoMovies = 1; WinMain state 3 calls PlayMovieInWindow(intro) unconditionally
        g.Expect(0x6011AC, "C7 05 04 3C 97 00 01 00 00 00 6A 00 6A 00 E8 71 9D EA FF");
        g.Expect(0x5FFDD9, "E8 52 06 00 00 59 59");
        if (g.Ok()) {
            *(uint32_t*)0x973C04 = 1;
            mem::Call(0x5FFDD9, (void*)&PlayIntroMovie);
        }
    }
    {  // video mode selection + window style per mode
        PatchGroup g("window: video mode / window style");
        g.Expect(game::psSelectDevice, "53 56 55 31 DB 83 EC 10 83 3D F4 2E 78 00 00");
        g.Expect(game::WinMain_RestyleWindow,
                 "8B 15 FC 38 9B 00 8B 02 68 00 00 00 80 6A F0 50 FF 15 08 15 6F 00 "
                 "8B 0D FC 38 9B 00 8B 01 6A 27 6A 00 6A 00 6A 00 6A 00 6A 00 50 FF 15 0C 15 6F 00");
        if (g.Ok()) {
            mem::Jmp(game::psSelectDevice, (void*)&SelectDevice);
            mem::Call(game::WinMain_RestyleWindow, (void*)&ApplyWindowMode);
            uint8_t jmp[2] = {0xEB, 0x2A};  // -> 0x5FFA01 (RsEventHandler(rsRWINITIALISE))
            mem::Write(game::WinMain_RestyleWindow + 5, jmp, 2);
        } else s_mode = Exclusive;
    }
    {  // DirectInput mouse always non-exclusive (menu close re-created it exclusive: dead mouse / no alt-tab)
        PatchGroup g("window: non-exclusive mouse");
        g.Expect(game::psInputMouseNeedsExclusive, "8B 0D EC 2E 78 00 83 EC 10");
        if (g.Ok()) {
            uint8_t b[3] = {0x30, 0xC0, 0xC3};
            mem::Write(game::psInputMouseNeedsExclusive, b, 3);
        }
    }
    {  // Audio Setup provider row: 0xFD and 0xFF have their own text, any other index goes to Get3DProviderName
        PatchGroup g("window: audio setup provider name");
        g.Expect(0x49ECF0, "8A 45 26 3C FD 0F 84 D3 00 00 00 3C FF 0F 84 8F 00 00 00 "
                           "B9 94 FB A0 00 50 E8 A2 AB 15 00 8D B4 24 90 00 00 00 56 50 E8 D4 36 0B 00");
        g.Expect(game::DMAudio_Get3DProviderName, "83 EC 08 8B 44 24 0C 50 89 4C 24 08 B9 20 C7 78 00");
        if (g.Ok()) mem::Call(0x49ED09, (void*)&Get3DProviderName);
    }
}

void Shutdown() {
    Unclip();
    if (s_keepAwake) SetThreadExecutionState(ES_CONTINUOUS);
}
}  // namespace window

// ---------------------------------------------------------------------------------------------------------------------
//  RENDER CALLBACK
// ---------------------------------------------------------------------------------------------------------------------

namespace render {
// Called by render.cpp after every successful CreateDevice; no window state depends on the device yet.
void OnDeviceCreated(void*, D3DPRESENT_PARAMETERS8*) {}
}  // namespace render
