@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fluidwp_vspath_wt.txt"
set /p VSPATH=<"%TEMP%\fluidwp_vspath_wt.txt"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake -S "%~dp0." -B "%~dp0build2" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build2" --target FluidWallpaper
