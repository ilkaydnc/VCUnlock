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
//   FILE      version.h
//   MODULE    version
//   ROLE      Executable versions and the Steam address map
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Every address in the source is written as its address in the Steam gta-vc.exe. At() turns it into the address in
//   the running executable: unchanged on Steam, through a range map on the other versions. An address the running
//   version has no counterpart for maps to 0, so the byte checks that guard every patch fail and the feature is
//   skipped instead of written to the wrong place.
//
//   Byte patterns can name addresses too, so the same pattern checks every version (see mem::Match):
//     [973B34]   the 4-byte absolute address At(0x973B34)
//     <4D0CB0>   a rel32 field that reaches At(0x4D0CB0)
//
//   Adding a version: generate its map into versions/<id>.inc, add a row to the list in version.cpp, then give the
//   patch groups that fail on it a variant where its code really differs.
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
#include <cstddef>
#include <utility>

namespace ver {

// Steam addresses [start, end) are at start + delta in the running executable.
struct Range {
    uint32_t start, end;
    int32_t delta;
};

// The known executables. Code that really differs in one of them (other registers, another stack frame) names it:
//   ver::Is(ver::JP) ? "pattern in the Japanese executable" : "pattern in the others"
enum Id { Steam, V10, JP };

struct Version {
    Id id;
    const char* name;
    uintptr_t identAt;     // identity: the dword at identAt in this executable is identValue once its code is readable,
    uint32_t identValue;   //   and packedValue before (Steam: SteamStub decrypts the code after VCUnlock loads)
    uint32_t packedValue;  //   0 if the code is never encrypted
    const Range* map;      // sorted by start; nullptr for the Steam executable itself
    size_t mapSize;
};

// Finds the running version. False while the code is still encrypted and for an executable that is no known version.
bool Detect();
const Version* Current();  // nullptr until Detect() succeeds
bool Is(Id id);            // the running executable is `id`
// A known version, readable now or once its code is decrypted. VCUnlock leaves any other executable alone.
bool Known();
}  // namespace ver

// The running executable's address for a Steam address; 0 if this version has none.
uintptr_t At(uintptr_t steam);

// A game function by its Steam address, callable like the function. It can be a namespace-scope constant: static
// initialisation runs before the version is known, so each call looks the address up.
//   constexpr GameFn<void(__cdecl*)(float)> SetFov{0x54A1D0};
template <class F> struct GameFn {
    uintptr_t steam;
    template <class... A> decltype(auto) operator()(A&&... a) const {
        return reinterpret_cast<F>(At(steam))(std::forward<A>(a)...);
    }
};
