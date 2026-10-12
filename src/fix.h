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
//   FILE      fix.h
//   MODULE    fix
//   ROLE      Building blocks for code patches
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Every patch that changes game code goes through here. Each one checks the original bytes first, refuses (and logs)
//   on a mismatch, and is counted in the install totals.
//
//   - Redirect      points an instruction's memory operand at a variable of ours.
//   - Scaled        points a float constant at a copy scaled for the current frame.
//   - Hook          mid-function hook through a thunk that saves every register, the flags and the FPU.
//   - RetargetCall  swaps the target of a call instruction.
//   - AllocCode     one 64 KB executable pool for generated code.
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
#include "core.h"
#include "timer.h"

namespace fix {

// ---------------------------------------------------------------------------------------------------------------------
//  OPERAND PATCHES
// ---------------------------------------------------------------------------------------------------------------------

// Rewrites the disp32 inside the instruction at `insn` that currently holds `oldAddr` so it reads `newAddr`.
// `pattern` must match the original instruction bytes. Returns false (and logs) on mismatch.
bool Redirect(uintptr_t insn, const char* pattern, uintptr_t oldAddr, const void* newAddr, const char* what);

// Redirects one instruction's float constant operand to a per-frame scaled copy of it.
// The base value is read from the game's constant (and checked against `expect` if non-zero).
bool Scaled(uintptr_t insn, const char* pattern, uintptr_t constAddr, timer::Scale mode, const char* what,
            float expect = 0);

// ---------------------------------------------------------------------------------------------------------------------
//  MID-FUNCTION HOOKS
// ---------------------------------------------------------------------------------------------------------------------

// Mid-function hook. The thunk saves all registers, flags and the FPU (fsave), calls
// handler(Regs*, Fpu*), restores, then runs the displaced instructions and jumps back.
// The handler may modify registers / FPU registers, or set r->cont to continue elsewhere
// (the displaced instructions are skipped then).
#pragma pack(push, 1)
struct Regs {
    uint32_t edi, esi, ebp, esp_, ebx, edx, ecx, eax;
    uint32_t eflags;
    uint32_t cont;  // continuation address (default: displaced code + jmp back)
    uint32_t* Stack() { return (uint32_t*)((uint8_t*)&cont + 4); }  // game's esp at the hook
};

struct Fpu {
    uint16_t cw, _r0, sw, _r1, tw, _r2;
    uint32_t ip, cs, dp, ds;
    uint8_t st[8][10];  // ST(0)..ST(7), 80-bit
    int Top() const { return (sw >> 11) & 7; }
    bool Empty(int i) const { return ((tw >> (((Top() + i) & 7) * 2)) & 3) == 3; }
    double Get(int i) const;
    void Set(int i, double v);
};
#pragma pack(pop)
static_assert(sizeof(Fpu) == 108, "fsave area");

using Handler = void(__cdecl*)(Regs*, Fpu*);

// `len` bytes at `at` (>= 5, whole instructions, no relative branches) are displaced into the thunk.
bool Hook(uintptr_t at, size_t len, const char* pattern, Handler h, const char* what);

// ---------------------------------------------------------------------------------------------------------------------
//  CALLS, CODE MEMORY AND TOTALS
// ---------------------------------------------------------------------------------------------------------------------

// Replaces the target of the E8 call at `at` (must currently call `oldTarget`).
bool RetargetCall(uintptr_t at, uintptr_t oldTarget, const void* to, const char* what);

// Executable memory for generated thunks (64 KB pool, 16-byte aligned).
uint8_t* AllocCode(size_t n);

// Count a patch applied elsewhere (or a failure) in the totals.
void Count(bool ok);

int Applied();
int Failed();
}  // namespace fix
