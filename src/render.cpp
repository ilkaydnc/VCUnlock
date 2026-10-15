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
//   FILE      render.cpp
//   MODULE    render
//   ROLE      Present, vsync and refresh-rate handling
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   - psCameraShowRaster passes a constant vsync flag. The original passed "limiter or menu", and every change of that
//     flag cost a full device Reset.
//   - IDirect3D8::CreateDevice and IDirect3DDevice8::Reset set the present interval and swap effect to match that flag.
//     Exclusive fullscreen runs at the highest (or the configured) refresh rate instead of the lowest one at or above
//     60 Hz.
//   - In exclusive fullscreen D3D8 ignores the present interval, so VCUnlock presents immediately and waits for the
//     vertical blank itself (D3DKMT).
//   - A Frame Limiter change applies at once: a windowed device swaps COPY and COPY_VSYNC through the game's own device
//     reset, exclusive fullscreen switches the vertical blank wait.
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
#include "d3d8min.h"

namespace render {

// ---------------------------------------------------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------------------------------------------------

static void*(__stdcall* s_Direct3DCreate8)(UINT);
static HRESULT(__stdcall* s_CreateDevice)(void*, UINT, DWORD, HWND, DWORD, D3DPRESENT_PARAMETERS8*, void**);
static HRESULT(__stdcall* s_Reset)(void*, D3DPRESENT_PARAMETERS8*);
static HRESULT(__stdcall* s_Present)(void*, const RECT*, const RECT*, HWND, const void*);
static void* s_d3d;
int g_refresh;
uint32_t g_presents;
double g_presentTime;

// ---------------------------------------------------------------------------------------------------------------------
//  VERTICAL BLANK WAIT
// ---------------------------------------------------------------------------------------------------------------------

// Windows' D3D8 ignores FullScreen_PresentationInterval in exclusive fullscreen (measured: 8000+
// presents/s with INTERVAL_ONE at 240 Hz; D3D9 and windowed D3D8 are fine). For exclusive fullscreen
// we therefore present IMMEDIATE and wait for the vertical blank ourselves (D3DKMT vblank event).
static bool s_vblankSync;  // wait for vblank before Present
static unsigned s_rwFlag;  // vsync flag passed to RwCameraShowRaster (must match the device state)
enum Path { Exclusive, WindowedCopy, WindowedDiscard };
static Path s_path;

struct KMT_OPEN {
    HDC hDc;
    UINT hAdapter;
    LUID luid;
    UINT vidPn;
};

struct KMT_WAIT {
    UINT hAdapter, hDevice, vidPn;
};

static KMT_WAIT s_kmtWait;
static LONG(WINAPI* s_kmtWaitFn)(const KMT_WAIT*);

static void InitVBlank(HWND wnd) {
    if (s_kmtWaitFn) return;
    HMODULE g = GetModuleHandleA("gdi32.dll");
    auto open = (LONG(WINAPI*)(KMT_OPEN*))GetProcAddress(g, "D3DKMTOpenAdapterFromHdc");
    auto wait = (LONG(WINAPI*)(const KMT_WAIT*))GetProcAddress(g, "D3DKMTWaitForVerticalBlankEvent");
    if (!open || !wait) {
        Log("vblank: D3DKMT not available");
        return;
    }
    MONITORINFOEXA mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(wnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    KMT_OPEN o = {};
    o.hDc = CreateDCA(mi.szDevice, mi.szDevice, nullptr, nullptr);
    LONG st = open(&o);
    if (o.hDc) DeleteDC(o.hDc);
    if (st != 0) {
        Log("vblank: open adapter failed %08X", (unsigned)st);
        return;
    }
    s_kmtWait.hAdapter = o.hAdapter;
    s_kmtWait.vidPn = o.vidPn;
    s_kmtWaitFn = wait;
    Log("vblank sync ready (%s, vidpn %u)", mi.szDevice, o.vidPn);
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENT PARAMETERS
// ---------------------------------------------------------------------------------------------------------------------

static int DesktopRefresh() {
    DEVMODEA dm = {};
    dm.dmSize = sizeof dm;
    return EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 60;
}

// Highest refresh rate available for WxH in `fmt` (or the configured one if available).
static UINT PickRefresh(void* d3d, UINT adapter, UINT w, UINT h, D3DFORMAT8 fmt) {
    UINT n = VCall<UINT>(d3d, vt::D3D_GetAdapterModeCount, adapter);
    UINT best = 0;
    bool want = false;
    for (UINT i = 0; i < n; ++i) {
        D3DDISPLAYMODE8 m;
        if (FAILED(VCall<HRESULT>(d3d, vt::D3D_EnumAdapterModes, adapter, i, &m))) continue;
        if (m.Width != w || m.Height != h || m.Format != fmt) continue;
        if (g_cfg.refreshRate > 0 && (int)m.RefreshRate == g_cfg.refreshRate) want = true;
        if (m.RefreshRate > best) best = m.RefreshRate;
    }
    return want ? (UINT)g_cfg.refreshRate : best;
}

static void AdjustParams(void* d3d, UINT adapter, D3DPRESENT_PARAMETERS8* pp, const char* who) {
    if (!pp->Windowed) {
        UINT hz = PickRefresh(d3d, adapter, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat);
        if (hz) pp->FullScreen_RefreshRateInHz = hz;
        pp->FullScreen_PresentationInterval = D3DPRESENT8_INTERVAL_IMMEDIATE;
        s_vblankSync = g_cfg.vsync != 0;
        s_rwFlag = 0;  // RW keeps IMMEDIATE, never resets
        s_path = Exclusive;
        g_refresh = (int)pp->FullScreen_RefreshRateInHz;
    } else if (pp->SwapEffect == D3DSWAPEFFECT8_COPY || pp->SwapEffect == D3DSWAPEFFECT8_COPY_VSYNC) {
        pp->SwapEffect = g_cfg.vsync ? D3DSWAPEFFECT8_COPY_VSYNC : D3DSWAPEFFECT8_COPY;
        s_vblankSync = false;
        s_rwFlag = g_cfg.vsync ? 1 : 0;  // matches COPY_VSYNC/COPY, RW never resets
        s_path = WindowedCopy;
        g_refresh = DesktopRefresh();
    } else {  // windowed with MSAA (DISCARD): no native vsync
        s_vblankSync = g_cfg.vsync != 0;
        s_rwFlag = 1;  // RW skips the reset for any swap effect != COPY
        s_path = WindowedDiscard;
        g_refresh = DesktopRefresh();
    }
    if (s_vblankSync) InitVBlank(pp->hDeviceWindow ? pp->hDeviceWindow : (HWND)game::RwD3D8Window());
    timer::SetRefresh(g_refresh);
    Log("%s: %ux%u fmt %u windowed %d swap %u refresh %u interval %08X ms %u -> display %d Hz", who,
        pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->Windowed, pp->SwapEffect,
        pp->FullScreen_RefreshRateInHz, pp->FullScreen_PresentationInterval, pp->MultiSampleType, g_refresh);
}

// VSync switched in the game menu.
void ApplyVsync() {
    if (s_path == WindowedCopy) {
        // RW sees the flag differ from the swap effect and resets the device; Reset then picks COPY_VSYNC or COPY
        s_rwFlag = g_cfg.vsync ? 1 : 0;
        return;
    }
    s_vblankSync = g_cfg.vsync != 0;
    if (s_vblankSync) InitVBlank((HWND)game::RwD3D8Window());
}

// ---------------------------------------------------------------------------------------------------------------------
//  DIRECT3D 8 HOOKS
// ---------------------------------------------------------------------------------------------------------------------

static HRESULT __stdcall Present(void* dev, const RECT* a, const RECT* b, HWND w, const void* r) {
    if (s_vblankSync && s_kmtWaitFn) s_kmtWaitFn(&s_kmtWait);
    double t0 = timer::Now();
    HRESULT hr = s_Present(dev, a, b, w, r);
    g_presentTime += timer::Now() - t0;
    ++g_presents;
    return hr;
}

static HRESULT __stdcall Reset(void* dev, D3DPRESENT_PARAMETERS8* pp) {
    AdjustParams(s_d3d, 0, pp, "Reset");
    HRESULT hr = s_Reset(dev, pp);
    Log("Reset -> %08X", (unsigned)hr);
    return hr;
}

static HRESULT __stdcall CreateDevice(void* d3d, UINT adapter, DWORD type, HWND wnd, DWORD flags,
                                      D3DPRESENT_PARAMETERS8* pp, void** out) {
    s_d3d = d3d;
    AdjustParams(d3d, adapter, pp, "CreateDevice");
    HRESULT hr = s_CreateDevice(d3d, adapter, type, wnd, flags, pp, out);
    Log("CreateDevice(adapter %u, flags %X) -> %08X", adapter, flags, (unsigned)hr);
    if (SUCCEEDED(hr) && *out) {
        if (void* o = mem::HookVtbl(*out, vt::Dev_Reset, (void*)&Reset)) s_Reset = (decltype(s_Reset))o;
        if (void* o = mem::HookVtbl(*out, vt::Dev_Present, (void*)&Present)) s_Present = (decltype(s_Present))o;
    }
    return hr;
}

static void* __stdcall Direct3DCreate8(UINT sdk) {
    void* d3d = s_Direct3DCreate8(sdk);
    if (d3d) {
        if (void* o = mem::HookVtbl(d3d, vt::D3D_CreateDevice, (void*)&CreateDevice))
            s_CreateDevice = (decltype(s_CreateDevice))o;
    }
    return d3d;
}

// ---------------------------------------------------------------------------------------------------------------------
//  CAMERA SHOW RASTER
// ---------------------------------------------------------------------------------------------------------------------

// Replaces psCameraShowRaster(RwCamera*): the vsync flag always matches the device state.
static void __cdecl CameraShowRaster(void* camera) {
    using Fn = void*(__cdecl*)(void*, void*, unsigned);
    void* window = *(void**)game::RsGlobalPs();
    ((Fn)At(game::RwCameraShowRaster))(camera, window, s_rwFlag);
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void InstallEarly(HMODULE exe) {
    s_Direct3DCreate8 = (decltype(s_Direct3DCreate8))mem::HookImport(exe, "d3d8.dll", "Direct3DCreate8",
                                                                     (void*)&Direct3DCreate8);
}

void Install() {
    PatchGroup g("render: psCameraShowRaster constant vsync");
    g.Expect(At(game::psCameraShowRaster), "80 3D [86865D] 00 53 8B 5C 24 08 75 09 80 3D [868670] 00");
    if (g.Ok()) mem::Jmp(At(game::psCameraShowRaster), (void*)&CameraShowRaster);
}
}  // namespace render
