@echo off
rem Builds HOGFixer (32-bit) with the MSVC toolchain from Visual Studio 2022 + bundled CMake/Ninja.
rem Usage: build.cmd [Release|Debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
  echo Visual Studio with C++ tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul || exit /b 1
set PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

cd /d "%~dp0"
if not exist third_party\SDL\CMakeLists.txt git submodule update --init --recursive
cmake -S . -B build\%CONFIG% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build build\%CONFIG% || exit /b 1
echo.
echo Build output: %~dp0build\%CONFIG%\bin
