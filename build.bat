@echo off
rem Builds Arrhythmetic and starts it. Double-click this file.
rem Needs CMake, Git and Visual Studio 2022 ("Desktop development with C++").

cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found. Install it from https://cmake.org and try again.
    pause
    exit /b 1
)

rem Windows locks a running .exe, so the build would fail at the very end.
:checkRunning
tasklist /fi "imagename eq Arrhythmetic.exe" | find /i "Arrhythmetic.exe" >nul
if not errorlevel 1 (
    echo Arrhythmetic is running. Save your pattern, close the app, then press any key.
    pause >nul
    goto checkRunning
)

if not exist build\CMakeCache.txt (
    echo First build: downloading JUCE and setting everything up. This takes a few minutes...
    cmake -B build
    if errorlevel 1 goto failed
)

cmake --build build --config Release
if errorlevel 1 goto failed

start "" "build\Arrhythmetic_artefacts\Release\Arrhythmetic.exe"
exit /b 0

:failed
echo.
echo The build failed. Copy the messages above and paste them to Claude.
pause
exit /b 1
