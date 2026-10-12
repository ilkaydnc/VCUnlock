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
//   FILE      render.h
//   MODULE    render
//   ROLE      Present, vsync and refresh-rate interface
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Import hooks go in at DllMain time, code patches after SteamStub has decrypted .text. The present counters feed the
//   stats line in timer.cpp; ApplyVsync and RefreshRates serve the Display Setup rows.
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
#include <cstdint>

struct D3DPRESENT_PARAMETERS8;

namespace render {
extern int g_refresh;         // display refresh rate in Hz
extern uint32_t g_presents;   // Present calls since the last stats line
extern double g_presentTime;  // seconds spent in Present since the last stats line

void InstallEarly(HMODULE exe);  // import hooks (DllMain time)
void Install();                  // code patches (after SteamStub decrypted .text)
void OnDeviceCreated(void* device, D3DPRESENT_PARAMETERS8* pp);  // after a successful CreateDevice
void ApplyVsync();                                               // after g_cfg.vsync changed
int RefreshRates(int w, int h, int* out, int max);               // 32-bit modes of WxH, ascending
}  // namespace render
