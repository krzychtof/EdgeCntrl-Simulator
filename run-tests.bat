@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH. Install it with: winget install Kitware.CMake
    exit /b 1
)

cmake --preset windows-mingw
if errorlevel 1 exit /b 1
cmake --build --preset windows-mingw --parallel
if errorlevel 1 exit /b 1
ctest --preset windows-mingw
exit /b %errorlevel%
