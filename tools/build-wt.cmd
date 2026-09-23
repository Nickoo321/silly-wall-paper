@echo off
setlocal
rem Configure + build build2\ (Ninja, Release) for the CURRENT DIRECTORY,
rem not this script's own location. This file lives in tools\ so it can be
rem committed (a copy at the worktree root is in .git\info\exclude, so fresh
rem worktrees never have one -- see AUDIT-2026-09-22.md item 4 / EXECUTOR-CARD
rem item 4). Run it from the worktree root: cmd /c "<repo>\tools\build-wt.cmd"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fluidwp_vspath_wt.txt"
set /p VSPATH=<"%TEMP%\fluidwp_vspath_wt.txt"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%CD%\build2\build.ninja" cmake -S "%CD%" -B "%CD%\build2" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%CD%\build2" --target FluidWallpaper || exit /b 1
