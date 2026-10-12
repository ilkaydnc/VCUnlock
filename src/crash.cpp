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
//   FILE      crash.cpp
//   MODULE    crash
//   ROLE      Fatal exception logger
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   A vectored exception handler that logs the first fatal exception: a stack overflow anywhere, or an access violation
//   in game code. It writes the registers, a printable dump of the stack top and the return-address candidates near the
//   stack base and above esp, each resolved to module+offset.
//
//   It never handles the exception. The game crashes exactly as it would have, but VCUnlock.log (written now, even with
//   Log=0) says where, without a debugger attached.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "core.h"
#include <cstdio>

namespace crash {
static bool s_done;

// ---------------------------------------------------------------------------------------------------------------------
//  HELPERS
// ---------------------------------------------------------------------------------------------------------------------

static const char* ModName(uintptr_t a, uintptr_t* base) {
    static char buf[MAX_PATH];
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)a, &m) || !m)
        return nullptr;
    *base = (uintptr_t)m;
    GetModuleFileNameA(m, buf, MAX_PATH);
    const char* n = strrchr(buf, 0x5C);
    return n ? n + 1 : buf;
}

static bool IsCode(uintptr_t a) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof mbi)) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
//  EXCEPTION HANDLER
// ---------------------------------------------------------------------------------------------------------------------

static LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    uintptr_t eip = ep->ContextRecord->Eip;
    bool fatal = code == EXCEPTION_STACK_OVERFLOW ||
                 (code == EXCEPTION_ACCESS_VIOLATION && (eip >= 0x401000 && eip < 0x700000));
    if (!fatal || s_done) return EXCEPTION_CONTINUE_SEARCH;
    s_done = true;
    LogToFile();
    uintptr_t base = 0;
    const char* mod = ModName(eip, &base);
    Log("CRASH code %08X at %08X (%s+%X) esp %08X ebp %08X eax %08X ecx %08X", (unsigned)code, (unsigned)eip,
        mod ? mod : "?", (unsigned)(eip - base), (unsigned)ep->ContextRecord->Esp, (unsigned)ep->ContextRecord->Ebp,
        (unsigned)ep->ContextRecord->Eax, (unsigned)ep->ContextRecord->Ecx);
    const uintptr_t* sp = (const uintptr_t*)ep->ContextRecord->Esp;
    // printable dump of the stack top (overflows from recursive string formatting show up here)
    {
        char t[257];
        const char* b = (const char*)sp;
        for (int i = 0; i < 256; ++i) {
            char c = b[i];
            t[i] = (c >= 32 && c < 127) ? c : '.';
        }
        t[256] = 0;
        Log("  stack text: %s", t);
    }
    // thread start: return addresses near the stack base
    {
        NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
        const uintptr_t* top = (const uintptr_t*)tib->StackBase;
        Log("  thread %u stack %p..%p", GetCurrentThreadId(), tib->StackLimit, tib->StackBase);
        int n = 0;
        for (const uintptr_t* q = top - 1; q > top - 4096 && n < 40; --q) {
            uintptr_t v = *q;
            if (v < 0x10000 || !IsCode(v)) continue;
            const char* m = ModName(v, &base);
            Log("  [base-%04X] %08X %s+%X", (unsigned)((top - q) * 4), (unsigned)v, m ? m : "?", (unsigned)(v - base));
            ++n;
        }
    }
    int found = 0;
    for (int i = 0; i < 4096 && found < 30; ++i) {
        uintptr_t v;
        __try {
            v = sp[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (v < 0x10000 || !IsCode(v)) continue;
        const char* m = ModName(v, &base);
        Log("  [esp+%04X] %08X %s+%X", i * 4, (unsigned)v, m ? m : "?", (unsigned)(v - base));
        ++found;
    }
    LogFlush();
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

void Install() {
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    AddVectoredExceptionHandler(1, Handler);
}
}  // namespace crash
