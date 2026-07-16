@echo off
setlocal

rem Usage: build.bat [Configuration] [Platform]
rem   Configuration: Debug | Release | Release_Production   (default: Debug)
rem   Platform:      x64 | x86                              (default: x64)

set "CONFIG=%~1"
set "PLATFORM=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe - is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VSINSTALL=%%i"
)

if not defined VSINSTALL (
    echo Could not find a Visual Studio installation with MSBuild.
    exit /b 1
)

set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo Could not find MSBuild.exe at "%MSBUILD%"
    exit /b 1
)

echo Building HotBiteEngine.sln [%CONFIG%^|%PLATFORM%]...
"%MSBUILD%" "%~dp0HotBiteEngine.sln" /m /p:Configuration=%CONFIG% /p:Platform=%PLATFORM%

exit /b %ERRORLEVEL%
