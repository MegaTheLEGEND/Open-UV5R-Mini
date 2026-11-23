@echo off
rem Builds the cable update file build\open-uv5r-mini.ufw (docs/BUILDING.md has the details).
rem Change these two lines if your MSYS2 or toolchain are somewhere else, or set them before running.
rem Extra arguments go to make, e.g.  build.bat ENABLE_CHIRP=0
if not defined MSYS_BIN set "MSYS_BIN=C:\msys64\usr\bin"
if not defined TOOLCHAIN set "TOOLCHAIN=E:/toolchains/pi32v2-llvm/bin"

if not exist "%MSYS_BIN%\make.exe" (
	echo make not found in %MSYS_BIN%. Install MSYS2 make or set MSYS_BIN.
	exit /b 1
)
if not exist "%TOOLCHAIN%" (
	echo Toolchain not found at %TOOLCHAIN%. Set TOOLCHAIN to pi32v2-llvm\bin.
	exit /b 1
)

rem MSYS2 goes at the end of PATH, so Windows Python and gcc are still found first.
set "PATH=%PATH%;%MSYS_BIN%"
pushd "%~dp0"
if "%~1"=="" (
	"%MSYS_BIN%\make.exe" ufw TOOLCHAIN=%TOOLCHAIN%
) else (
	"%MSYS_BIN%\make.exe" ufw TOOLCHAIN=%TOOLCHAIN% %*
)
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (
	echo Build failed.
	exit /b %RC%
)
echo.
echo Done: build\open-uv5r-mini.ufw
