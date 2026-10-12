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
//   FILE      d3d8min.h
//   MODULE    render
//   ROLE      Minimal Direct3D 8 declarations
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The Windows SDK no longer ships d3d8.h. These are the few structures, constants and vtable slots VCUnlock touches,
//   laid out as the D3D8 ABI defines them. VCall calls a COM method by vtable index.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#pragma once
#include <windows.h>

// ---------------------------------------------------------------------------------------------------------------------
//  STRUCTURES
// ---------------------------------------------------------------------------------------------------------------------

typedef DWORD D3DFORMAT8;
struct D3DPRESENT_PARAMETERS8 {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT8 BackBufferFormat;
    UINT BackBufferCount;
    DWORD MultiSampleType;
    DWORD SwapEffect;  // 1 DISCARD, 2 FLIP, 3 COPY, 4 COPY_VSYNC
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT8 AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;  // 0x80000000 IMMEDIATE, 1 ONE
};
static_assert(sizeof(D3DPRESENT_PARAMETERS8) == 0x34, "D3D8 present params");

struct D3DDISPLAYMODE8 {
    UINT Width, Height, RefreshRate;
    D3DFORMAT8 Format;
};

// ---------------------------------------------------------------------------------------------------------------------
//  CONSTANTS
// ---------------------------------------------------------------------------------------------------------------------

enum {
    D3DSWAPEFFECT8_DISCARD = 1,
    D3DSWAPEFFECT8_FLIP = 2,
    D3DSWAPEFFECT8_COPY = 3,
    D3DSWAPEFFECT8_COPY_VSYNC = 4,
};

constexpr D3DFORMAT8 D3DFMT8_A8R8G8B8 = 21, D3DFMT8_X8R8G8B8 = 22;
constexpr UINT D3DPRESENT8_INTERVAL_ONE = 1;
constexpr UINT D3DPRESENT8_INTERVAL_IMMEDIATE = 0x80000000;
constexpr HRESULT D3DERR8_DEVICELOST = (HRESULT)0x88760868;
constexpr HRESULT D3DERR8_DEVICENOTRESET = (HRESULT)0x88760869;

// ---------------------------------------------------------------------------------------------------------------------
//  VTABLE SLOTS
// ---------------------------------------------------------------------------------------------------------------------

namespace vt {
// IDirect3D8
constexpr int D3D_GetAdapterModeCount = 6, D3D_EnumAdapterModes = 7, D3D_GetAdapterDisplayMode = 8,
              D3D_CreateDevice = 15;
// IDirect3DDevice8
constexpr int Dev_TestCooperativeLevel = 3, Dev_Reset = 14, Dev_Present = 15;
}  // namespace vt

// Calls the COM method in vtable slot `idx` of `obj`.
template <class R, class... A> inline R VCall(void* obj, int idx, A... a) {
    using F = R(__stdcall*)(void*, A...);
    return ((F)(*(void***)obj)[idx])(obj, a...);
}
