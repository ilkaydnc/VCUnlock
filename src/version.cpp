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
//   FILE      version.cpp
//   MODULE    version
//   ROLE      Executable versions and the Steam address map
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The identity dwords are the ones the community tools use to tell the executables apart (the same address holds
//   0xB85548EC in each of 1.0, 1.1 and Steam, at a different place). On Steam it sits in .text, so it only matches
//   after SteamStub has decrypted the code, which makes it the decryption check as well; before that the same dword
//   holds its encrypted value, which still tells the Steam executable from an unknown one.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "version.h"
#include <windows.h>
#include <iterator>

namespace ver {

// ---------------------------------------------------------------------------------------------------------------------
//  KNOWN VERSIONS
// ---------------------------------------------------------------------------------------------------------------------

namespace {
constexpr Range kMap10[] = {
#include "versions/vc_10.inc"
};

constexpr Version kVersions[] = {
    {"Steam", 0x666BA5, 0xB85548EC, 0xEB698EFC, nullptr, 0},
    {"1.0", 0x667BF5, 0xB85548EC, 0, kMap10, std::size(kMap10)},
};

const Version* s_current;
const Range* s_map;
const Range* s_mapEnd;

bool ReadDword(uintptr_t at, uint32_t* v) {
    __try {
        *v = *(const uint32_t*)at;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
//  DETECTION
// ---------------------------------------------------------------------------------------------------------------------

bool Detect() {
    if (s_current) return true;
    for (const Version& v : kVersions) {
        uint32_t d;
        if (!ReadDword(v.identAt, &d) || d != v.identValue) continue;
        s_map = v.map;
        s_mapEnd = v.map + v.mapSize;
        s_current = &v;
        return true;
    }
    return false;
}

const Version* Current() { return s_current; }

bool Known() {
    if (Detect()) return true;
    for (const Version& v : kVersions) {
        uint32_t d;
        if (v.packedValue && ReadDword(v.identAt, &d) && d == v.packedValue) return true;
    }
    return false;
}
}  // namespace ver

// ---------------------------------------------------------------------------------------------------------------------
//  ADDRESS MAP
// ---------------------------------------------------------------------------------------------------------------------

uintptr_t At(uintptr_t steam) {
    using namespace ver;
    if (!s_map) return steam;
    const Range* lo = s_map;  // the last range starting at or below `steam`
    const Range* hi = s_mapEnd;
    while (lo + 1 < hi) {
        const Range* mid = lo + (hi - lo) / 2;
        (mid->start <= steam ? lo : hi) = mid;
    }
    return lo->start <= steam && steam < lo->end ? steam + lo->delta : 0;
}
