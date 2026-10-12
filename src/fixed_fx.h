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
//   FILE      fixed_fx.h
//   MODULE    fixed
//   ROLE      World effects drawn from the fixed 30 Hz world
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Driven by fixed.cpp around every tick and every drawn frame.
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
#include <cstdint>
#include "fixed_math.h"

namespace fixed {
namespace fx {
void Install(const uint32_t* presentationClock);   // the clock effects read while a frame is drawn
void Reset();                                      // the world (re)starts: forget the tick history
void BeforeTick(uint32_t index);                   // index = ticks already run in this drawn frame
void AfterTick();                                  // capture the effect state the tick wrote
void BeginDraw(float alpha, uint32_t frameTicks);  // before the camera is presented
void AfterCamera(fixedmath::Vec drawnCamera);      // the camera is at its drawn position
void EndDraw();                                    // the clock and the camera are back at the tick
#ifdef VCUNLOCK_FIXED_TEST
bool LayoutsVerified();
#endif
}  // namespace fx
}  // namespace fixed
