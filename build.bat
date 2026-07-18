@echo off
setlocal
rem Locate VS Build Tools and build with CMake + Ninja.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found - install VS 2022 Build Tools first.
    exit /b 1
)
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fluidwp_vspath.txt"
set /p VSPATH=<"%TEMP%\fluidwp_vspath.txt"
if not defined VSPATH (
    echo ERROR: no VS installation with C++ tools found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
