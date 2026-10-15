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
//   ROLE      Entry points of the HUD, menu and per-frame modules
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Called by main.cpp in install order.
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

// The FPS counter.
namespace hud {
void Install();
bool FpsReady();  // FPS counter patch in place
}  // namespace hud

// Display Setup rows for the VCUnlock settings.
namespace menu {
void Install(bool world);
}  // namespace menu

// Frame-rate fixes for code that runs once per drawn frame.
namespace perframe {
void Install();
}  // namespace perframe
