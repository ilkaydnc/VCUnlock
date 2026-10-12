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
//   FILE      fixed.h
//   MODULE    fixed
//   ROLE      The fixed 30 Hz world: install entry point
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Install verifies every layout and hook site first and patches nothing unless all of them match. Test builds
//   (VCUNLOCK_FIXED_TEST) also get the testing:: entry points.
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

namespace fixed {
bool Install();  // false: byte checks failed, nothing patched (the game keeps its own timing)
void FrameDelta(double scaledMs);  // real time of this drawn frame in ms, times the time scale (0 while paused)

#ifdef VCUNLOCK_FIXED_TEST
// Test builds: drive the world and the presentation without the game loop.
namespace testing {
void Reset();
void ProcessFrame(double scaledMs);
void BeginDraw();
void EndDraw();
uint32_t FrameTicks();
float Alpha();
double DroppedMs();
bool ParticleSuppressed();
void ParticleAdded(void* particle);
void RenderParticles();
uint32_t WaveMs();
}  // namespace testing
#endif
}  // namespace fixed
