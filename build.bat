@echo off
rem ====================================================================================================================
rem
rem      _    __________  __      __           __
rem     | |  / / ____/ / / /___  / /___  _____/ /__
rem     | | / / /   / / / / __ \/ / __ \/ ___/ //_/        GRAND THEFT AUTO: VICE CITY
rem     | |/ / /___/ /_/ / / / / / /_/ / /__/ ,<           Uncapped frame rate.
rem     |___/\____/\____/_/ /_/_/\____/\___/_/|_|
rem
rem ====================================================================================================================
rem
rem   FILE      build.bat
rem   MODULE    build
rem   ROLE      Builds dinput8.dll (x86) and its VCUnlock.asi copy into build
rem
rem --------------------------------------------------------------------------------------------------------------------
rem
rem   Finds the latest Visual Studio with the C++ x86 tools and builds a release DLL with a PDB.
rem
rem --------------------------------------------------------------------------------------------------------------------
rem
rem   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
rem             SPDX-License-Identifier: MIT
rem   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
rem             Interactive. Requires an original copy of the game.
rem
rem ====================================================================================================================

setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with the C++ x86 tools was not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++20 /O2 /MT /arch:SSE2 /fp:precise /GS- /EHs-c- /utf-8 /W3 /wd4996 /DNDEBUG /Zi ^
   /Fobuild\ /Fdbuild\vc.pdb ^
   src\*.cpp /LD /Febuild\dinput8.dll ^
   /link /DEF:src\dinput8.def /DEBUG /OPT:REF /OPT:ICF user32.lib kernel32.lib gdi32.lib winmm.lib advapi32.lib || exit /b 1
rem the same DLL, for players whose dinput8.dll is already an ASI loader
copy /y build\dinput8.dll build\VCUnlock.asi >nul || exit /b 1
echo BUILD OK
