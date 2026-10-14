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
//   FILE      core.cpp
//   MODULE    core
//   ROLE      Logging, settings and memory patching primitives
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Implementation of core.h. Until LogToFile, the log keeps its first 48 KB and its most recent 48 KB in memory. It
//   goes to a file only after a crash, a failed byte check or with Log=1; from then on every line is flushed, so it
//   survives a crash.
//
//   The settings live next to gta_vc.set, in the folder the game itself uses (Documents\GTA Vice City User Files). An
//   older VCUnlock.ini in the game folder is taken over once and deleted with the old log.
//
//   mem::Match runs under SEH: checking an unmapped address fails the check instead of crashing the game.
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
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <share.h>

// ---------------------------------------------------------------------------------------------------------------------
//  GLOBALS
// ---------------------------------------------------------------------------------------------------------------------

Config g_cfg;
char g_dir[MAX_PATH];
char g_userDir[MAX_PATH];

// ---------------------------------------------------------------------------------------------------------------------
//  LOG
// ---------------------------------------------------------------------------------------------------------------------

// Until LogToFile, lines stay in memory: the first 48 KB, then a ring with the most recent 48 KB.
static FILE* s_log;
static CRITICAL_SECTION s_logCs;
static LARGE_INTEGER s_logT0, s_logFreq;
static char s_head[48 * 1024], s_tail[48 * 1024];
static size_t s_headLen, s_tailPos;
static bool s_tailWrapped;
static uint64_t s_tailBytes;

static struct LogInit {
    LogInit() {
        InitializeCriticalSection(&s_logCs);
        QueryPerformanceFrequency(&s_logFreq);
        QueryPerformanceCounter(&s_logT0);
    }
} s_logInit;

static void KeepInMemory(const char* line, size_t n) {
    if (!s_tailBytes && s_headLen + n <= sizeof s_head) {
        memcpy(s_head + s_headLen, line, n);
        s_headLen += n;
        return;
    }
    s_tailBytes += n;
    while (n) {
        size_t k = sizeof s_tail - s_tailPos;
        if (k > n) k = n;
        memcpy(s_tail + s_tailPos, line, k);
        line += k;
        n -= k;
        s_tailPos += k;
        if (s_tailPos == sizeof s_tail) {
            s_tailPos = 0;
            s_tailWrapped = true;
        }
    }
}

void Log(const char* fmt, ...) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    double sec = double(t.QuadPart - s_logT0.QuadPart) / double(s_logFreq.QuadPart);
    char line[2048];
    int n = sprintf_s(line, "[%9.3f] ", sec);
    va_list ap;
    va_start(ap, fmt);
    int room = (int)sizeof line - n - 1;
    int m = vsnprintf(line + n, room, fmt, ap);
    va_end(ap);
    n += (m < 0 || m >= room) ? room - 1 : m;
    line[n++] = '\n';
    EnterCriticalSection(&s_logCs);
    if (s_log) {
        fwrite(line, 1, n, s_log);
        fflush(s_log);
    } else KeepInMemory(line, n);
    LeaveCriticalSection(&s_logCs);
}

void LogFlush() {
    if (s_log) fflush(s_log);
}

void LogToFile() {
    EnterCriticalSection(&s_logCs);
    if (!s_log) {
        char p[MAX_PATH];
        strcpy_s(p, g_userDir[0] ? g_userDir : g_dir);
        strcat_s(p, "VCUnlock.log");
        s_log = _fsopen(p, "w", _SH_DENYWR);
        if (s_log) {
            fwrite(s_head, 1, s_headLen, s_log);
            if (s_tailWrapped) {
                fprintf(s_log, "[...] %llu bytes not kept\n", (unsigned long long)(s_tailBytes - sizeof s_tail));
                fwrite(s_tail + s_tailPos, 1, sizeof s_tail - s_tailPos, s_log);
            }
            fwrite(s_tail, 1, s_tailPos, s_log);
            fflush(s_log);
        }
    }
    LeaveCriticalSection(&s_logCs);
}

// ---------------------------------------------------------------------------------------------------------------------
//  SETTINGS
// ---------------------------------------------------------------------------------------------------------------------

static int IniInt(const char* path, const char* sec, const char* key, int def) {
    return (int)GetPrivateProfileIntA(sec, key, def, path);
}

static void IniPut(const char* path, const char* sec, const char* key, int v) {
    char b[16];
    sprintf_s(b, "%d", v);
    WritePrivateProfileStringA(sec, key, b, path);
}

static const char kDefaultIni[] =
    "; Grand Theft Auto: Vice City display settings. Display Setup and Audio Setup in the game change them too.\r\n"
    "[Display]\r\n"
    "; 0 = exclusive fullscreen, 1 = borderless fullscreen window, 2 = windowed\r\n"
    "WindowMode=1\r\n"
    "; resolution, 0 = desktop resolution (windowed: 0 = 75% of desktop)\r\n"
    "Width=0\r\n"
    "Height=0\r\n"
    "; exclusive fullscreen refresh rate in Hz, 0 = highest available\r\n"
    "RefreshRate=0\r\n"
    "; 1 = vsync on (frame rate follows the monitor refresh rate), 0 = off\r\n"
    "VSync=1\r\n"
    "; frame rate cap, 0 = unlimited (applies on top of vsync if lower)\r\n"
    "FPSLimit=0\r\n"
    "; 1 = HUD, radar and text at the correct aspect ratio and a wider view on wide screens\r\n"
    "Widescreen=1\r\n"
    "; 1 = show the frame rate in the top-left corner\r\n"
    "ShowFPS=0\r\n"
    "[Window]\r\n"
    "; 1 = open the pause menu when the game loses focus, 0 = keep running in the background\r\n"
    "PauseOnFocusLoss=1\r\n"
    "; 1 = mute audio while the game is in the background (borderless/windowed)\r\n"
    "MuteOnFocusLoss=1\r\n"
    "; 1 = skip the startup logo movies\r\n"
    "SkipIntro=0\r\n"
    "[Debug]\r\n"
    "; 1 = write VCUnlock.log, with frame rate statistics every 5 seconds\r\n"
    "Log=0\r\n";

// The folder the game keeps gta_vc.set in, found the way the game finds it.
static void FindUserDir() {
    char p[MAX_PATH] = {};
    DWORD n = sizeof p - 32, type = 0;
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0,
                      KEY_READ, &k) != ERROR_SUCCESS)
        return;
    LONG r = RegQueryValueExA(k, "Personal", nullptr, &type, (BYTE*)p, &n);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ || !p[0]) return;
    strcat_s(p, "\\GTA Vice City User Files");
    CreateDirectoryA(p, nullptr);
    DWORD a = GetFileAttributesA(p);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) sprintf_s(g_userDir, "%s\\", p);
}

static void IniPath(char* p, const char* dir) {
    strcpy_s(p, MAX_PATH, dir);
    strcat_s(p, MAX_PATH, "VCUnlock.ini");
}

static void ReadConfig(const char* p) {
    Config& c = g_cfg;
    c.windowMode = IniInt(p, "Display", "WindowMode", c.windowMode);
    c.width = IniInt(p, "Display", "Width", c.width);
    c.height = IniInt(p, "Display", "Height", c.height);
    c.refreshRate = IniInt(p, "Display", "RefreshRate", c.refreshRate);
    c.vsync = IniInt(p, "Display", "VSync", c.vsync);
    c.fpsLimit = IniInt(p, "Display", "FPSLimit", c.fpsLimit);
    c.widescreen = IniInt(p, "Display", "Widescreen", c.widescreen);
    c.showFps = IniInt(p, "Display", "ShowFPS", c.showFps);
    c.pauseOnFocusLoss = IniInt(p, "Window", "PauseOnFocusLoss", c.pauseOnFocusLoss);
    c.muteOnFocusLoss = IniInt(p, "Window", "MuteOnFocusLoss", c.muteOnFocusLoss);
    c.skipIntro = IniInt(p, "Window", "SkipIntro", c.skipIntro);
    c.log = IniInt(p, "Debug", "Log", c.log);
}

static void WriteConfig(const char* p, bool all) {
    const Config& c = g_cfg;
    IniPut(p, "Display", "WindowMode", c.windowMode);
    IniPut(p, "Display", "Width", c.width);
    IniPut(p, "Display", "Height", c.height);
    IniPut(p, "Display", "RefreshRate", c.refreshRate);
    IniPut(p, "Display", "VSync", c.vsync);
    IniPut(p, "Display", "FPSLimit", c.fpsLimit);
    IniPut(p, "Display", "Widescreen", c.widescreen);
    IniPut(p, "Display", "ShowFPS", c.showFps);
    IniPut(p, "Window", "MuteOnFocusLoss", c.muteOnFocusLoss);
    if (!all) return;
    IniPut(p, "Window", "PauseOnFocusLoss", c.pauseOnFocusLoss);
    IniPut(p, "Window", "SkipIntro", c.skipIntro);
}

void SaveConfig() {
    char p[MAX_PATH];
    IniPath(p, g_userDir[0] ? g_userDir : g_dir);
    WriteConfig(p, false);
}

void LoadConfig(HMODULE self) {
    GetModuleFileNameA(self, g_dir, MAX_PATH);
    if (char* s = strrchr(g_dir, '\\')) s[1] = 0;
    FindUserDir();
    char p[MAX_PATH], old[MAX_PATH];
    IniPath(p, g_userDir[0] ? g_userDir : g_dir);
    IniPath(old, g_dir);
    // older versions kept the file (and the log) in the game folder
    bool migrate = g_userDir[0] && GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES;
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileA(p, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, kDefaultIni, sizeof kDefaultIni - 1, &w, nullptr);
            CloseHandle(h);
        }
        if (migrate) {
            ReadConfig(old);
            g_cfg.widescreen = IniInt(old, "HUD", "WidescreenHUD", 1) && IniInt(old, "HUD", "WidescreenFOV", 1);
            g_cfg.showFps = IniInt(old, "HUD", "ShowFPS", g_cfg.showFps);
            WriteConfig(p, true);
        }
    }
    ReadConfig(p);
    if (g_cfg.windowMode < 0 || g_cfg.windowMode > 2) g_cfg.windowMode = 1;
    if (migrate) {
        DeleteFileA(old);
        strcpy_s(old, g_dir);
        strcat_s(old, "VCUnlock.log");
        DeleteFileA(old);
    }
    if (g_cfg.log) LogToFile();
}

// ---------------------------------------------------------------------------------------------------------------------
//  MEMORY PATCHING
// ---------------------------------------------------------------------------------------------------------------------

namespace mem {
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "[973B34]" / "<4D0CB0>": a Steam address, 6 hex digits, through At(); 0 if the pattern is malformed or this version
// has no such address
static uintptr_t PlaceholderAddr(const char*& p) {
    char close = p[0] == '[' ? ']' : '>';
    uintptr_t steam = 0;
    ++p;
    for (; *p && *p != close; ++p) {
        int v = hexv(*p);
        if (v < 0) return 0;
        steam = steam * 16 + v;
    }
    if (*p != close) return 0;
    ++p;
    return At(steam);
}

bool Match(uintptr_t addr, const char* p) {
    const uint8_t* m = (const uint8_t*)addr;
    __try {
        while (*p) {
            while (*p == ' ') ++p;
            if (!*p) break;
            if (p[0] == '?') {
                p += (p[1] == '?') ? 2 : 1;
                ++m;
                continue;
            }
            if (p[0] == '[' || p[0] == '<') {
                bool rel = p[0] == '<';
                uintptr_t a = PlaceholderAddr(p);
                if (!a) return false;
                if (rel) a -= uintptr_t(m) + 4;
                if (*(const uint32_t*)m != (uint32_t)a) return false;
                m += 4;
                continue;
            }
            int hi = hexv(p[0]), lo = hexv(p[1]);
            if (hi < 0 || lo < 0) return false;
            if (*m != uint8_t(hi * 16 + lo)) return false;
            p += 2;
            ++m;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

size_t PatternSize(const char* p) {
    size_t n = 0;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        n += *p == '[' || *p == '<' ? 4 : 1;
        while (*p && *p != ' ') ++p;
    }
    return n;
}

void Write(uintptr_t addr, const void* data, size_t n) {
    DWORD old;
    VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)addr, data, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
}

void Nop(uintptr_t addr, size_t n) {
    uint8_t b[64];
    memset(b, 0x90, sizeof b);
    while (n) {
        size_t k = n > 64 ? 64 : n;
        Write(addr, b, k);
        addr += k;
        n -= k;
    }
}

static void Rel(uint8_t op, uintptr_t at, const void* to, size_t len) {
    uint8_t b[32];
    memset(b, 0x90, sizeof b);
    b[0] = op;
    int32_t rel = int32_t(uintptr_t(to) - (at + 5));
    memcpy(b + 1, &rel, 4);
    Write(at, b, len < 5 ? 5 : len);
}

void Jmp(uintptr_t at, const void* to, size_t len) { Rel(0xE9, at, to, len); }
void Call(uintptr_t at, const void* to, size_t len) { Rel(0xE8, at, to, len); }
uintptr_t RelTarget(uintptr_t at) { return at + 5 + *(int32_t*)(at + 1); }

void** FindImport(HMODULE mod, const char* dll, const char* func) {
    auto base = (uint8_t*)mod;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp((char*)(base + d->Name), dll) != 0) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, func) == 0) return (void**)&iat->u1.Function;
        }
    }
    return nullptr;
}

void* HookImport(HMODULE mod, const char* dll, const char* func, void* hook) {
    void** slot = FindImport(mod, dll, func);
    if (!slot) {
        Log("import %s!%s not found", dll, func);
        return nullptr;
    }
    void* orig = *slot;
    Write((uintptr_t)slot, &hook, sizeof hook);
    return orig;
}

void* HookVtbl(void* obj, int index, void* hook) {
    void** vt = *(void***)obj;
    void* orig = vt[index];
    if (orig != hook) Write((uintptr_t)&vt[index], &hook, sizeof hook);
    return orig == hook ? nullptr : orig;
}
}  // namespace mem

// ---------------------------------------------------------------------------------------------------------------------
//  PATCH GROUPS
// ---------------------------------------------------------------------------------------------------------------------

PatchGroup& PatchGroup::Expect(uintptr_t addr, const char* pattern) {
    ++checks;
    if (!mem::Match(addr, pattern)) {
        if (ok) Log("[%s] byte check FAILED at %08X (expected %s)", name, (unsigned)addr, pattern);
        ok = false;
    }
    return *this;
}

static int s_groupFailures;
int PatchGroupFailures() { return s_groupFailures; }

bool PatchGroup::Ok() {
    Log("[%s] %s (%d checks)", name, ok ? "applied" : "SKIPPED", checks);
    if (!ok) ++s_groupFailures;
    return ok;
}
