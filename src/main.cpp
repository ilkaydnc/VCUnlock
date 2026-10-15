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
//   FILE      main.cpp
//   MODULE    main
//   ROLE      dinput8.dll proxy and ASI plugin: entry point and install order
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The game loads VCUnlock as dinput8.dll from its own folder. DirectInput8Create is forwarded to the real dinput8.dll
//   in the system directory, so input works exactly as before. The same DLL renamed to VCUnlock.asi is loaded by an ASI
//   loader instead, for players whose dinput8.dll is already their loader.
//
//   As dinput8.dll, DllMain runs while SteamStub still holds .text encrypted, so it only hooks imports. The code
//   patches go in on the first early import call (GetStartupInfoA, GetCommandLineA, GetModuleHandleA) made after .text
//   has been decrypted; the game's own DirectInput8Create call from WinMain is the fallback trigger. An ASI loader loads
//   plugins after the decryption, from one of those early calls, so there DllMain installs at once.
//
//   With both files installed, the first copy loaded owns the game and the second one stays inert.
//
//   Versions: the executable is identified first (version.h). An executable that is no known version is left alone:
//   no hook and no patch, only a log line that names it.
//
//   Install order: the fixed 30 Hz world first. If any of its byte checks fail, the game keeps its own 30 fps timing
//   and only the vsync handling and the FPS counter are applied.
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
#include "game.h"
#include "timer.h"
#include "render.h"
#include "modules.h"
#include "fixed.h"
#include "fix.h"
#include <cstdio>

namespace crash {
void Install();
}  // namespace crash

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

static HMODULE s_self, s_realDinput;
static bool s_active, s_installed;
static HRESULT(WINAPI* s_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, IUnknown*);

static void InstallAll(const char* from) {
    if (!s_active || s_installed) return;
    if (!ver::Detect()) return;  // the code is still encrypted
    s_installed = true;
    Log("gta-vc.exe %s, installing (trigger: %s)", ver::Current()->name, from);
    // The fixed 30 Hz world replaces CTimer::Update and the frame loop; if its byte checks fail the game
    // keeps its own timing (30 fps limiter) and only the vsync handling and the FPS counter are applied.
    bool world = fixed::Install();
    render::Install();
    if (world) perframe::Install();
    hud::Install();
    menu::Install(world);
    Log("install done");
    if (PatchGroupFailures() || fix::Failed()) LogToFile();  // a different executable: keep the log
}

// ---------------------------------------------------------------------------------------------------------------------
//  DECRYPTION TRIGGERS
// ---------------------------------------------------------------------------------------------------------------------

// Early CRT imports: the first one called after SteamStub has decrypted .text installs everything.
static void(WINAPI* s_GetStartupInfoA)(LPSTARTUPINFOA);
static LPSTR(WINAPI* s_GetCommandLineA)();
static HMODULE(WINAPI* s_GetModuleHandleA)(LPCSTR);

static void WINAPI Hk_GetStartupInfoA(LPSTARTUPINFOA si) {
    InstallAll("GetStartupInfoA");
    s_GetStartupInfoA(si);
}

static LPSTR WINAPI Hk_GetCommandLineA() {
    InstallAll("GetCommandLineA");
    return s_GetCommandLineA();
}

static HMODULE WINAPI Hk_GetModuleHandleA(LPCSTR n) {
    InstallAll("GetModuleHandleA");
    return s_GetModuleHandleA(n);
}

// ---------------------------------------------------------------------------------------------------------------------
//  DIRECTINPUT PROXY
// ---------------------------------------------------------------------------------------------------------------------

static bool LoadReal() {
    if (s_realDinput) return true;
    char p[MAX_PATH];
    GetSystemDirectoryA(p, MAX_PATH);
    strcat_s(p, "\\dinput8.dll");
    s_realDinput = LoadLibraryA(p);
    if (s_realDinput)
        s_DirectInput8Create = (decltype(s_DirectInput8Create))GetProcAddress(s_realDinput, "DirectInput8Create");
    return s_DirectInput8Create != nullptr;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID iid, LPVOID* out,
                                                   IUnknown* outer) {
    InstallAll("DirectInput8Create");
    if (!LoadReal()) return E_FAIL;
    return s_DirectInput8Create(inst, ver, iid, out, outer);
}

// ---------------------------------------------------------------------------------------------------------------------
//  DLL ENTRY POINT
// ---------------------------------------------------------------------------------------------------------------------

static bool IsViceCity() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    const char* n = strrchr(p, '\\');
    n = n ? n + 1 : p;
    return _stricmp(n, "gta-vc.exe") == 0;
}

// Size, link time and entry point name the executable in a report.
static void LogUnsupported() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    GetFileAttributesExA(p, GetFileExInfoStandard, &fa);
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    Log("unsupported executable (%lu bytes, timestamp %08lX, entry %08lX): VCUnlock does nothing", fa.nFileSizeLow,
        nt->FileHeader.TimeDateStamp, nt->OptionalHeader.AddressOfEntryPoint);
    LogToFile();
}

// dinput8.dll and VCUnlock.asi both installed: only the first copy loaded may patch the game.
static bool FirstCopy() {
    char name[64];
    sprintf_s(name, "Local\\VCUnlock.%lu", GetCurrentProcessId());
    HANDLE h = CreateMutexA(nullptr, FALSE, name);  // kept open for the life of the process
    return !h || GetLastError() != ERROR_ALREADY_EXISTS;
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_self = mod;
        DisableThreadLibraryCalls(mod);
        if (!IsViceCity() || !FirstCopy()) return TRUE;
        LoadConfig(mod);
        char file[MAX_PATH];
        GetModuleFileNameA(mod, file, MAX_PATH);
        const char* name = strrchr(file, '\\');
        Log("VCUnlock loaded as %s (vsync %d, fps limit %d)", name ? name + 1 : file, g_cfg.vsync, g_cfg.fpsLimit);
        if (!ver::Known()) {
            LogUnsupported();
            return TRUE;
        }
        s_active = true;
        crash::Install();
        HMODULE exe = GetModuleHandleA(nullptr);
        s_GetStartupInfoA = (decltype(s_GetStartupInfoA))mem::HookImport(exe, "KERNEL32.dll", "GetStartupInfoA",
                                                                         (void*)&Hk_GetStartupInfoA);
        s_GetCommandLineA = (decltype(s_GetCommandLineA))mem::HookImport(exe, "KERNEL32.dll", "GetCommandLineA",
                                                                         (void*)&Hk_GetCommandLineA);
        s_GetModuleHandleA = (decltype(s_GetModuleHandleA))mem::HookImport(exe, "KERNEL32.dll", "GetModuleHandleA",
                                                                           (void*)&Hk_GetModuleHandleA);
        render::InstallEarly(exe);
        InstallAll("DllMain");  // loaded as an ASI plugin: .text is already decrypted
    } else if (reason == DLL_PROCESS_DETACH && s_active) {
        Log("unloading");
    }
    return TRUE;
}
