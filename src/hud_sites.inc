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
//   FILE      hud_sites.inc
//   MODULE    hud
//   ROLE      2D draw sites that take the aspect-correct 1/640 scale
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Each row is {instruction, constant}: the instruction reads the 1/640 constant at that address, and hud.cpp
//   redirects it to a copy scaled for the current aspect ratio.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

// CHud::DrawAfterFade (0x5565D0): script text, big messages, the mission title
{0x55665E, 0x696A78},
{0x5566F8, 0x696A78},
{0x5568EC, 0x696A78},
{0x556913, 0x696A78},
{0x556A03, 0x696A78},
{0x556A2A, 0x696A78},
{0x556CE6, 0x696A78},
{0x556D10, 0x696A78},
{0x556E40, 0x696A78},
{0x556E6C, 0x696A78},
{0x556ED8, 0x696A78},
{0x556F14, 0x696A78},
{0x5571C3, 0x696A78},

// CHud::Draw (0x557210): the HUD, the radar disc and the sniper scope
{0x557568, 0x696A78},
{0x55764C, 0x696A78},
{0x557769, 0x696A78},
{0x55788D, 0x696A78},
{0x557A8A, 0x696A78},
{0x557B2E, 0x696A78},
{0x557BD2, 0x696A78},
{0x557FA0, 0x696A78},
{0x5580DB, 0x696A78},
{0x5581EC, 0x696A78},
{0x5584B1, 0x696A78},
{0x55859B, 0x696A78},
{0x558641, 0x696A78},
{0x558802, 0x696A78},
{0x5588A1, 0x696A78},
{0x558AED, 0x696A78},
{0x558B4A, 0x696A78},
{0x558BC9, 0x696A78},
{0x558C2B, 0x696A78},
{0x558D5D, 0x696A78},
{0x558DBA, 0x696A78},
{0x558E39, 0x696A78},
{0x558EBE, 0x696A78},
{0x558F2A, 0x696A78},
{0x5590A8, 0x696A78},
{0x559221, 0x696A78},
{0x559318, 0x696A78},
{0x5593AA, 0x696A78},
{0x5596FF, 0x696A78},
{0x55972F, 0x696A78},
{0x55977D, 0x696A78},
{0x55989F, 0x696A78},
{0x559BD1, 0x696A78},
{0x559C01, 0x696A78},
{0x559C4F, 0x696A78},
{0x559D6B, 0x696A78},
{0x559DCB, 0x696A78},
{0x559ECE, 0x696A78},
{0x559FC6, 0x696A78},
{0x55A049, 0x696A78},
{0x55A0BA, 0x696A78},
{0x55A178, 0x696A78},
{0x55A260, 0x696A78},
{0x55A369, 0x696A78},
{0x55A428, 0x696A78},
{0x55A4E8, 0x696A78},
{0x55A581, 0x696A78},
{0x55A639, 0x696A78},
{0x55A6F8, 0x696A78},
{0x55A890, 0x696A78},
{0x55A9C9, 0x696A78},
{0x55AA8E, 0x696A78},
{0x55AB28, 0x696A78},
{0x55AE12, 0x696A78},
{0x55AE48, 0x696A78},
{0x55AF10, 0x696A78},
{0x55AF3D, 0x696A78},
{0x55AFC6, 0x696A78},
{0x55B3CF, 0x696A78},
{0x55B3FF, 0x696A78},
{0x55B457, 0x696A78},
{0x55B54C, 0x696A78},
{0x55B60F, 0x696A78},
{0x55B63C, 0x696A78},
{0x55B664, 0x696A78},
{0x55BA5C, 0x696A78},
{0x55BA8C, 0x696A78},
{0x55BB8F, 0x696A78},

// CMBlur::AddRenderFx (0x55D050): the radar exclusion rectangle, in step with the radar
{0x55D19A, 0x696DC8},
{0x55D380, 0x696DC8},

// CRadar: the mask, the map tiles, the blips, their icons and traces
{0x4C1A2E, 0x68ED1C},
{0x4C1B72, 0x68ED1C},
{0x4C1FF1, 0x68ED1C},
{0x4C41B4, 0x68ED1C},
{0x4C43EF, 0x68ED1C},
{0x4C4822, 0x68ED1C},
{0x4C5337, 0x68ED1C},
{0x4C57E7, 0x68ED1C},
{0x4C2BD9, 0x68ED1C},
{0x4C2D5A, 0x68ED1C},
{0x4C3292, 0x68ED1C},
{0x4C3363, 0x68ED1C},
{0x4C3415, 0x68ED1C},
{0x4C34F0, 0x68ED1C},
{0x4C3576, 0x68ED1C},
{0x4C35FD, 0x68ED1C},

// CDarkel::DrawMessages (0x429FB0): rampage messages. The Japanese executable has no black shadow prints (0x42A319,
// 0x42A48E): it draws that shadow with the font's drop shadow instead.
{0x42A057, 0x6864E4},
{0x42A118, 0x6864E4},
{0x42A219, 0x6864E4},
{0x42A319, 0x6864E4, ver::JP},
{0x42A386, 0x6864E4},
{0x42A425, 0x6864E4},
{0x42A48E, 0x6864E4, ver::JP},
{0x42A519, 0x6864E4},

// CGarages::PrintMessages (0x42F280)
{0x42F2C5, 0x6866E4},

// Render2dStuff (0x4A6060): the scope side bars, in step with the scope in CHud::Draw
{0x4A62E7, 0x68D5C8},
{0x4A633B, 0x68D5C8},

// LoadingScreen (0x4A68A0): the loading text
{0x4A6BC8, 0x68D5C8},

// CSpecialFX::Render2DFXs (0x573E10): the video camera text
{0x573E40, 0x69841C},
{0x5741C8, 0x69841C},

// cMusicManager::DisplayRadioStationName (0x5F9B40)
{0x5F9DBC, 0x6CC9B4},

// CReplay::Display (0x620820)
{0x620886, 0x6DA66C},
