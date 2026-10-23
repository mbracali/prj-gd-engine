@echo off
rem SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
rem Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
rem Double-click to build the Windows download into gd-game-01\local_builds\.
rem Needs MSYS2 (https://www.msys2.org) with, in its UCRT64 shell:
rem   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL3 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja zip
setlocal
if "%MSYS2%"=="" set "MSYS2=C:\msys64"
if not exist "%MSYS2%\usr\bin\bash.exe" (
  echo MSYS2 was not found at %MSYS2%. Install it from https://www.msys2.org or set MSYS2 to its folder.
  pause
  exit /b 1
)
set "MSYSTEM=UCRT64"
set "CHERE_INVOKING=1"
cd /d "%~dp0.."
"%MSYS2%\usr\bin\bash.exe" -lc "./local_build.sh windows"
echo.
if errorlevel 1 (echo The build failed, see the messages above.) else (echo Done. The builds are in %CD%\local_builds)
pause
