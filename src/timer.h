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
//   FILE      timer.h
//   MODULE    timer
//   ROLE      Clocks of the fixed 30 Hz world and per-frame scale factors
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   k and logicalPassed tell per-frame code how much world time the current update stands for: exactly one tick inside
//   a world tick, the number of ticks just run on a drawn frame (usually 0 above 30 fps).
//
//   Scaled constants are float operands in game code redirected to a value refreshed after every update of k: Pow gives
//   c^k (per-frame decay), Linear gives c*k (per-frame step).
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

// Timing shared by the fixed 30 Hz world and the per-frame fixes (updated by every world tick and CTimer::Update)
namespace timer {

// ---------------------------------------------------------------------------------------------------------------------
//  FRAME STATE
// ---------------------------------------------------------------------------------------------------------------------

extern float k;                 // update length in 30 fps frames: 1 in a world tick, the tick count on a drawn frame
extern uint32_t logicalPassed;  // 30 Hz ticks in this update (0 on drawn frames without a tick)
extern float kFrame;            // this drawn frame's real length in 30 fps frames (0 while the world is stopped)
extern const float ts30;        // 50/30: the time step of one 30 Hz tick

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL AND CLOCK
// ---------------------------------------------------------------------------------------------------------------------

bool Verify();            // the CTimer::Update and WinMain limiter bytes match (patches nothing)
void Install();           // replaces CTimer::Update and the WinMain limiter; call only after Verify()
void SetRefresh(int hz);  // display refresh rate (from render module)
double Now();             // seconds (QPC)

// ---------------------------------------------------------------------------------------------------------------------
//  SCALED CONSTANTS
// ---------------------------------------------------------------------------------------------------------------------

// Per-frame scaled constants: the address of `value` replaces a constant operand in game code. Pow c^k | Linear c*k
enum class Scale { Pow, Linear };
struct ScaledConst {
    float base;
    Scale mode;
    float value;
};

ScaledConst* AddScaled(float base, Scale mode);
void OnFrame(void (*fn)());        // called after every update of k and logicalPassed
void OnDrawnFrame(void (*fn)());  // called once per drawn frame, after kFrame is set and before its world ticks

// ---------------------------------------------------------------------------------------------------------------------
//  WORLD TICKS
// ---------------------------------------------------------------------------------------------------------------------

// Fixed 30 Hz world: a tick advances the game clocks; a drawn frame only refreshes the per-frame values.
void FixedTick();
void FixedRender(uint32_t ticks);
}  // namespace timer
