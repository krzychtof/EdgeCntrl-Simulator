@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo Install CMake with: winget install Kitware.CMake
    echo Then restart the terminal and run this script again.
    exit /b 1
)

cmake --preset windows-mingw
if errorlevel 1 exit /b 1

cmake --build --preset windows-mingw --parallel
if errorlevel 1 exit /b 1

ctest --preset windows-mingw
if errorlevel 1 exit /b 1

echo.
echo Build and tests passed. Starting the simulator...
build\windows-mingw\edgecontrol.exe --scenario all
exit /b %errorlevel%
