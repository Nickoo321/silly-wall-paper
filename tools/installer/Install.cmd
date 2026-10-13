@echo off
rem Fluid Wallpaper setup. Works from the unpacked zip (install.ps1 next to this
rem file) and from the Setup exe (which carries payload.zip + this file).
setlocal
set "FW_HERE=%~dp0"
if exist "%FW_HERE%install.ps1" goto run_here
if exist "%FW_HERE%payload.zip" goto run_zip
echo Setup files are missing next to Install.cmd.
echo If you opened the .zip, first extract ALL of it (right-click the zip, "Extract All..."),
echo then double-click Install.cmd in the extracted folder.
pause
exit /b 1

:run_here
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%FW_HERE%install.ps1" %*
goto done

:run_zip
set "FW_ZIP=%FW_HERE%payload.zip"
set "FW_X=%TEMP%\FluidWallpaper-setup-%RANDOM%%RANDOM%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory($env:FW_ZIP, $env:FW_X)"
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%FW_X%\install.ps1" %*
set "FW_RC=%ERRORLEVEL%"
rmdir /s /q "%FW_X%" >nul 2>&1
if not "%FW_RC%"=="0" goto failed
exit /b 0

:done
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo Setup did not finish. The message above says why.
pause
exit /b 1
