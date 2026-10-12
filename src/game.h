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
//   FILE      game.h
//   MODULE    game
//   ROLE      Addresses in the Steam gta-vc.exe
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Every game variable and function VCUnlock uses, by its address in the Steam executable. GVAR declares a reference
//   to a game global, so game::TimeInMs reads and writes the game's own variable.
//
//   Each address was verified in the Steam binary. Code that patches near one checks the bytes again at runtime, so a
//   different executable is refused instead of corrupted.
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

#define GVAR(type, name, addr) inline type& name = *reinterpret_cast<type*>(addr)

namespace game {

// ---------------------------------------------------------------------------------------------------------------------
//  CTIMER
// ---------------------------------------------------------------------------------------------------------------------

GVAR(uint32_t, TimeInMs, 0x973B34);
GVAR(uint32_t, TimeInMsNonClipped, 0xA0C89C);
GVAR(uint32_t, TimeInMsPauseMode, 0x69001C);
GVAR(uint32_t, PrevTimeInMs, 0x977D94);
GVAR(float, TimeStep, 0x97442C);
GVAR(float, TimeStepNonClipped, 0x9743B0);
GVAR(float, TimeScale, 0x97E26C);
GVAR(uint8_t, UserPause, 0xA0FB3F);
GVAR(uint8_t, CodePause, 0xA0FB80);
GVAR(uint32_t, FrameCounter, 0xA0C8A0);
GVAR(uint8_t, SnapShotActive, 0xA0FAE5);        // CSpecialFX::bSnapShotActive
GVAR(uint16_t, RecordForGameState, 0xA0F93E);   // CRecordDataForGame::RecordingState (2 = playback)
GVAR(uint8_t, RecordForChaseStatus, 0xA0FBA2);  // CRecordDataForChase::Status (1 = recording)
GVAR(uint32_t, OldPerfCounterLo, 0x704300);     // CTimer::_oldPerfCounter
GVAR(uint32_t, OldPerfCounterHi, 0x704304);
GVAR(uint32_t, CyclesPerMs, 0x690018);

constexpr uintptr_t CTimer_Update = 0x4D0DF0;
constexpr uintptr_t CTimer_GetCurrentTimeInCycles = 0x4D0CB0;
constexpr uintptr_t CTimer_GetCyclesPerMillisecond = 0x4D0CF0;

// ---------------------------------------------------------------------------------------------------------------------
//  SKELETON, FRONTEND AND DEVICE SELECTION
// ---------------------------------------------------------------------------------------------------------------------

GVAR(int32_t, RsGlobalMaxFPS, 0x9B38F4);
GVAR(int32_t, RsGlobalMaxWidth, 0x9B38E4);  // window creation, startup camera size, mouse centring
GVAR(int32_t, RsGlobalMaxHeight, 0x9B38E8);
GVAR(int32_t, RsGlobalWidth, 0x9B38EC);  // SCREEN_WIDTH (written by CameraSize)
GVAR(int32_t, RsGlobalHeight, 0x9B38F0);
// CDraw
GVAR(float, MsFov, 0x695660);          // CDraw::ms_fFOV (horizontal, 4:3)
GVAR(float, MsAspectRatio, 0x94CD40);  // CDraw::ms_fAspectRatio
GVAR(void*, RsGlobalPs, 0x9B38FC);     // psGlobalType*: +0 window, +4 instance, +8 fullScreen
GVAR(uint8_t, PrefsFrameLimiter, 0x86865D);
GVAR(uint8_t, MenuActive, 0x868670);
GVAR(uint8_t, MenuStartUpRequested, 0x86864A);  // CMenuManager::m_bStartUpFrontEndRequested (opens pause menu)
GVAR(uint8_t, MenuGameNotLoaded, 0x8686A4);
GVAR(int32_t, MenuCurrOption, 0x868668);
GVAR(int32_t, MenuPrefsVideoMode, 0x868694);
GVAR(int32_t, MenuDisplayVideoMode, 0x868698);
GVAR(int32_t, MenuCurrScreen, 0x868730);  // CMenuManager::m_nCurrScreen
GVAR(int32_t, PrefsLanguage, 0x868688);   // 0 English, 1 French, 2 German, 3 Italian, 4 Spanish
constexpr uintptr_t MenuScreens = 0x6D7B40;  // CMenuScreen[]: 0xE2 bytes, a 10-byte header and 12 entries of 18 bytes
constexpr uintptr_t TheText = 0x94A228;
constexpr uintptr_t CText_Get = 0x584D60;  // thiscall (key), returns the 16-bit text
GVAR(int32_t, DefaultFullscreenRes, 0x6D49F0);
// psSelectDevice state
GVAR(int32_t, GnumSubSystems, 0x782EE4);
GVAR(int32_t, GcurSel, 0x782EE8);
GVAR(int32_t, GcurSelVM, 0x782EEC);
GVAR(int32_t, UseDefaultVM, 0x782EF4);
constexpr uintptr_t GsubSysInfo = 0x7829E4;  // RwSubSystemInfo[16], 0x50 bytes each
GVAR(void*, RwEngineInstance, 0x7860C8);     // +0x13C calloc(n, size)
GVAR(int32_t, ForegroundApp, 0x6D49F4);
GVAR(int32_t, GameState, 0x9B4F10);
GVAR(int32_t, RwInitialised, 0x973B7C);
GVAR(uint32_t, RwRequestedMaxRefresh, 0x6DCDBC);  // RwD3D8EngineSetRefreshRate target

// ---------------------------------------------------------------------------------------------------------------------
//  WINMAIN AND THE IDLE LOOP
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t WinMain_LimiterCheck = 0x6000D7;  // cmp byte [PrefsFrameLimiter],0 ... jne skip
constexpr uintptr_t WinMain_RunFrame = 0x6000FC;      // push 1; push rsIDLE; call RsEventHandler
constexpr uintptr_t psCameraShowRaster = 0x601DF0;
constexpr uintptr_t RwCameraShowRaster = 0x6499B0;
constexpr uintptr_t GetBestRefreshRate = 0x600C20;
constexpr uintptr_t Idle = 0x4A5C50;
constexpr uintptr_t FrontendIdle = 0x4A5B30;
constexpr uintptr_t Idle_CallTimerUpdate_Ret = 0x4A5C5C;  // return address of CTimer::Update in Idle
constexpr uintptr_t FrontendIdle_CallTimerUpdate_Ret = 0x4A5B3F;

// ---------------------------------------------------------------------------------------------------------------------
//  RENDERWARE ENGINE API (cdecl)
// ---------------------------------------------------------------------------------------------------------------------

struct RwVideoMode {
    int32_t width, height, depth;
    uint32_t flags;  // bit0 = exclusive
};

constexpr uintptr_t RwEngineGetNumSubSystems = 0x641A30;
constexpr uintptr_t RwEngineGetSubSystemInfo = 0x641A60;
constexpr uintptr_t RwEngineGetCurrentSubSystem = 0x641A90;
constexpr uintptr_t RwEngineSetSubSystem = 0x641AC0;
constexpr uintptr_t RwEngineGetNumVideoModes = 0x641AF0;
constexpr uintptr_t RwEngineGetVideoModeInfo = 0x641B20;
constexpr uintptr_t RwEngineSetVideoMode = 0x641B80;
constexpr uintptr_t RwD3D8EngineSetRefreshRate = 0x65CB70;
constexpr uintptr_t psGetVideoModeList = 0x6014F0;
constexpr uintptr_t psSelectDevice = 0x600A00;
constexpr uintptr_t psInputMouseNeedsExclusive = 0x6013B0;
// SetWindowLongA(WS_POPUP) + SetWindowPos, 49 bytes, resumes at 0x5FFA01
constexpr uintptr_t WinMain_RestyleWindow = 0x5FF9D0;
constexpr uintptr_t DMAudio_ReacquireDigitalHandle = 0x5F97D0;  // thiscall (this unused)
constexpr uintptr_t DMAudio_ReleaseDigitalHandle = 0x5F97F0;
constexpr uintptr_t DMAudio_Get3DProviderName = 0x5F98B0;  // thiscall (index), NULL for an invalid index
constexpr uintptr_t DMAudio = 0xA0FB94;

// ---------------------------------------------------------------------------------------------------------------------
//  D3D8 STATE OWNED BY RENDERWARE
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t RwD3D8PresentParams = 0xA0ED0C;
GVAR(void*, RwD3D8Device, 0x7887B0);
GVAR(void*, RwD3D8Object, 0x7887BC);
GVAR(void*, RwD3D8Window, 0x7887AC);
}  // namespace game
