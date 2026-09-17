@echo off
setlocal
rem Build the build2/ tree (Ninja, Release) with the VS environment. Never touches build/.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fluidwp_vspath2.txt"
set /p VSPATH=<"%TEMP%\fluidwp_vspath2.txt"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake --build "%~dp0build2" --target %1
