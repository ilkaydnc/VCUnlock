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
//   FILE      modules.h
//   MODULE    main
//   ROLE      Entry points of the window, HUD and per-frame modules
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Called by main.cpp in install order; timer.cpp also calls into window:: once per drawn frame.
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

// Window modes, focus, cursor and system settings.
namespace window {
void InstallEarly(HMODULE exe);
void Install();
void Shutdown();
void Tick();          // once per frame: cursor confinement
int BackgroundFps();  // frame cap while unfocused (0 = none)
int Mode();           // effective window mode

// Display Setup's Screen Resolution row. mode: 0 exclusive, 1 borderless, 2 windowed; hz 0 = default; vm = RW mode.
struct Choice {
    int mode, w, h, hz, vm;
};
int Choices(const Choice** list);      // rebuilds the list, returns its length
int CurrentChoice();                   // index of the running mode in the last list, -1 if none
void PrepareChoice(const Choice& c);   // takes the settings; the game's handler then restarts the device
bool SwitchWindowed(const Choice& c);  // borderless <-> windowed without a device restart; false if not possible
}  // namespace window

// Widescreen field of view, aspect-correct HUD and the FPS counter.
namespace hud {
void Install();
bool WidescreenReady();  // widescreen patches in place
bool FpsReady();         // FPS counter patch in place
void ApplyWidescreen();  // after g_cfg.widescreen changed
}  // namespace hud

// Display Setup and Audio Setup rows for the VCUnlock settings.
namespace menu {
void Install(bool world);
}  // namespace menu

// Frame-rate fixes for code that runs once per drawn frame.
namespace perframe {
void Install();
}  // namespace perframe
