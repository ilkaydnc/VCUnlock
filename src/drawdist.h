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
//   FILE      drawdist.h
//   MODULE    drawdist
//   ROLE      Draw distance and building fade-in
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

namespace drawdist {

void Install(bool world);  // the fade-in fix needs the fixed world's frame length

// ---------------------------------------------------------------------------------------------------------------------
//  STATE (read and switched by the developer probe to compare with the original behaviour)
// ---------------------------------------------------------------------------------------------------------------------

extern bool g_lodFix;          // scan distance follows the draw distance (false: the original 300 m)
extern bool g_fadeFix;         // fade step follows the frame length (false: 16 per drawn frame)
extern float g_lodLimit;       // scan distance limit at the normal 70 degree field of view, in metres
extern float g_lodDistance;    // scan distance in use
extern uint32_t g_fadeStep;    // alpha added per visibility check this frame
extern uint32_t g_visibleLost;  // entities left out because the visible list was full (total)

bool Ready();                 // the draw distance patches are in place
void SetScanDistance(float multiplier);  // from TheCamera.LODDistMultiplier (called by CCamera::Process)
void UpdateFadeStep();        // once per drawn frame (registered by Install)
void* const* VisibleList();   // CRenderer's visible list (ours once moved)
int VisibleCapacity();
}  // namespace drawdist
