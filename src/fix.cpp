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
//   FILE      fix.cpp
//   MODULE    fix
//   ROLE      Building blocks for code patches
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Implementation of fix.h, including the small x86 emitter that writes the Hook thunks.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "fix.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace fix {

// ---------------------------------------------------------------------------------------------------------------------
//  TOTALS
// ---------------------------------------------------------------------------------------------------------------------

static int s_applied, s_failed;
int Applied() { return s_applied; }
int Failed() { return s_failed; }
void Count(bool ok) { ++(ok ? s_applied : s_failed); }

// ---------------------------------------------------------------------------------------------------------------------
//  OPERAND PATCHES
// ---------------------------------------------------------------------------------------------------------------------

static size_t PatternLen(const char* p) {
    size_t n = 0;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        while (*p && *p != ' ') ++p;
        ++n;
    }
    return n;
}

static bool Check(uintptr_t insn, const char* pattern, const char* what) {
    if (mem::Match(insn, pattern)) return true;
    char have[64] = {};
    const uint8_t* b = (const uint8_t*)insn;
    for (int i = 0; i < 12; ++i) sprintf_s(have + i * 3, 4, "%02X ", b[i]);
    Log("fix [%s] byte check FAILED at %08X: want %s, have %s", what, (unsigned)insn, pattern, have);
    ++s_failed;
    return false;
}

bool Redirect(uintptr_t insn, const char* pattern, uintptr_t oldAddr, const void* newAddr, const char* what) {
    if (!Check(insn, pattern, what)) return false;
    size_t n = PatternLen(pattern);
    for (size_t i = 1; i + 4 <= n; ++i) {
        if (*(uint32_t*)(insn + i) == (uint32_t)oldAddr) {
            mem::Put<uint32_t>(insn + i, (uint32_t)(uintptr_t)newAddr);
            ++s_applied;
            return true;
        }
    }
    Log("fix [%s] operand %08X not found in instruction at %08X", what, (unsigned)oldAddr, (unsigned)insn);
    ++s_failed;
    return false;
}

bool Scaled(uintptr_t insn, const char* pattern, uintptr_t constAddr, timer::Scale mode, const char* what,
            float expect) {
    float base = *(const float*)constAddr;
    if (expect != 0 && fabsf(base - expect) > fabsf(expect) * 1e-5f) {
        Log("fix [%s] constant at %08X is %g, expected %g", what, (unsigned)constAddr, base, expect);
        ++s_failed;
        return false;
    }
    timer::ScaledConst* c = timer::AddScaled(base, mode);
    if (!c) {
        ++s_failed;
        return false;
    }
    return Redirect(insn, pattern, constAddr, &c->value, what);
}

// ---------------------------------------------------------------------------------------------------------------------
//  FPU REGISTER ACCESS
// ---------------------------------------------------------------------------------------------------------------------

// Handlers run with a fresh FPU after fsave; ST(i) lives in the saved area as an 80-bit value.
double Fpu::Get(int i) const {
    const void* p = st[i];
    double d;
    __asm { mov eax, p
            fld tbyte ptr [eax]
            fstp d }
    return d;
}

void Fpu::Set(int i, double v) {
    void* p = st[i];
    __asm { mov eax, p
            fld v
            fstp tbyte ptr [eax] }
}

// ---------------------------------------------------------------------------------------------------------------------
//  CALL RETARGETING
// ---------------------------------------------------------------------------------------------------------------------

bool RetargetCall(uintptr_t at, uintptr_t oldTarget, const void* to, const char* what) {
    if (*(const uint8_t*)at != 0xE8 || mem::RelTarget(at) != oldTarget) {
        Log("fix [%s] call at %08X does not target %08X", what, (unsigned)at, (unsigned)oldTarget);
        ++s_failed;
        return false;
    }
    mem::Call(at, to);
    ++s_applied;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  MID-FUNCTION HOOKS
// ---------------------------------------------------------------------------------------------------------------------

static uint8_t* s_code;
static size_t s_codeUsed;

uint8_t* AllocCode(size_t n) {
    if (!s_code) s_code = (uint8_t*)VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s_code || s_codeUsed + n > 0x10000) return nullptr;
    uint8_t* p = s_code + s_codeUsed;
    s_codeUsed += (n + 15) & ~15u;
    return p;
}

static bool Relative(const uint8_t* b) {
    // displaced code must not start with a relative branch/call (would need relocation)
    uint8_t o = b[0];
    return o == 0xE8 || o == 0xE9 || o == 0xEB || (o >= 0x70 && o <= 0x7F) || (o == 0x0F && (b[1] & 0xF0) == 0x80);
}

bool Hook(uintptr_t at, size_t len, const char* pattern, Handler h, const char* what) {
    if (len < 5 || !Check(at, pattern, what)) return false;
    if (Relative((const uint8_t*)at)) {
        Log("fix [%s] displaced code is relative", what);
        ++s_failed;
        return false;
    }
    uint8_t* t = AllocCode(64 + len);
    if (!t) {
        ++s_failed;
        return false;
    }
    uint8_t* p = t;
    auto u8 = [&](uint8_t v) { *p++ = v; };
    auto u32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };

    // The thunk. Regs and Fpu are the saved register and fsave areas, in stack order.
    uint8_t* contSlot;
    u8(0x68); contSlot = p; u32(0);                                 // push cont
    u8(0x9C);                                                       // pushfd
    u8(0x60);                                                       // pushad
    u8(0x83); u8(0xEC); u8(0x6C);                                   // sub esp,108
    u8(0x9B); u8(0xDD); u8(0x34); u8(0x24);                         // fsave [esp]
    u8(0x54);                                                       // push esp            (Fpu*)
    u8(0x8D); u8(0x44); u8(0x24); u8(0x70);                         // lea eax,[esp+112]   (Regs*)
    u8(0x50);                                                       // push eax
    u8(0xE8); u32(uint32_t((uintptr_t)h - ((uintptr_t)p + 4)));     // call handler
    u8(0x83); u8(0xC4); u8(0x08);                                   // add esp,8
    u8(0xDD); u8(0x24); u8(0x24);                                   // frstor [esp]
    u8(0x83); u8(0xC4); u8(0x6C);                                   // add esp,108
    u8(0x61);                                                       // popad
    u8(0x9D);                                                       // popfd
    u8(0xC3);                                                       // ret -> cont

    // Default continuation: the displaced instructions, then back to the game.
    uint32_t cont = (uint32_t)(uintptr_t)p;
    memcpy(contSlot, &cont, 4);
    memcpy(p, (const void*)at, len); p += len;                      // displaced instructions
    u8(0xE9); u32(uint32_t((at + len) - ((uintptr_t)p + 4)));       // jmp back

    FlushInstructionCache(GetCurrentProcess(), t, p - t);
    mem::Jmp(at, t, len);
    ++s_applied;
    return true;
}
}  // namespace fix
