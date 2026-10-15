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
//   FILE      core.h
//   MODULE    core
//   ROLE      Logging, settings and memory patching primitives
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Shared by every module:
//
//   - Log keeps timestamped lines in memory; LogToFile writes them to VCUnlock.log and keeps writing there.
//   - Config mirrors VCUnlock.ini next to VCUnlock.asi; LoadConfig writes a commented default file on the first
//     run and SaveConfig stores what the menus change.
//   - mem:: is the byte-level layer: pattern match, protected writes, jmp/call, import and vtable hooks.
//   - PatchGroup checks every byte a feature relies on before anything is written, so a feature is applied whole or not
//     at all.
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

#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "version.h"

// ---------------------------------------------------------------------------------------------------------------------
//  LOG
// ---------------------------------------------------------------------------------------------------------------------

void Log(const char* fmt, ...);
void LogFlush();
// Lines are kept in memory until this is called; then VCUnlock.log is written with everything logged so far and every
// later line goes straight to the file. Called at start-up with Log=1, on a crash and when a byte check fails.
void LogToFile();

// ---------------------------------------------------------------------------------------------------------------------
//  SETTINGS
// ---------------------------------------------------------------------------------------------------------------------

// VCUnlock.ini. The defaults match the file LoadConfig writes on the first run.
struct Config {
    // [Display]
    int refreshRate = 0;  // exclusive fullscreen only, 0 = highest available
    int vsync = 1;
    int fpsLimit = 0;  // 0 = unlimited (used when vsync off, or as an extra cap)
    int showFps = 0;
    // [Debug]
    int log = 0;
};

extern Config g_cfg;
void LoadConfig(HMODULE self);
void SaveConfig();               // writes the settings the game menus change
extern char g_dir[MAX_PATH];     // folder of VCUnlock.asi, with trailing backslash

// ---------------------------------------------------------------------------------------------------------------------
//  MEMORY PATCHING
// ---------------------------------------------------------------------------------------------------------------------

namespace mem {
// pattern: "55 A1 ?? 3B" (hex bytes, ?? = wildcard); "[973B34]" is the 4-byte address At(0x973B34) and "<4D0CB0>" a
// rel32 field that reaches At(0x4D0CB0) (see version.h)
bool Match(uintptr_t addr, const char* pattern);
size_t PatternSize(const char* pattern);  // bytes the pattern covers
void Write(uintptr_t addr, const void* data, size_t n);
template <class T> inline void Put(uintptr_t addr, T v) { Write(addr, &v, sizeof(T)); }
void Nop(uintptr_t addr, size_t n);
void Jmp(uintptr_t at, const void* to, size_t len = 5);   // jmp rel32 + nop padding
void Call(uintptr_t at, const void* to, size_t len = 5);  // call rel32 + nop padding
// read the rel32 target of an E8/E9 at `at`
uintptr_t RelTarget(uintptr_t at);
void** FindImport(HMODULE mod, const char* dll, const char* func);
void* HookImport(HMODULE mod, const char* dll, const char* func, void* hook);  // returns original
void* HookVtbl(void* obj, int index, void* hook);                              // returns original
}  // namespace mem

// ---------------------------------------------------------------------------------------------------------------------
//  PATCH GROUPS
// ---------------------------------------------------------------------------------------------------------------------

// A named group of patches applied only if every expected byte pattern matches.
struct PatchGroup {
    const char* name;
    bool ok = true;
    int checks = 0;
    explicit PatchGroup(const char* n) : name(n) {}
    PatchGroup& Expect(uintptr_t addr, const char* pattern);
    bool Ok();  // logs the result
};
int PatchGroupFailures();
