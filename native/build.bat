@echo off
rem Builds PULSE with MSVC (Visual Studio 2022 or later, "Desktop development with C++").
rem   build.bat                -> native\pulse.dll (same as "build.bat dll")
rem   build.bat test           -> the DLL, then model_test (the heart on made-up sessions), plugin_test (the
rem                               session against the SDK's fake live state: hearts, cards, report, screen effect)
rem                               and fx_test (fx\pulse_core.hlsli, the shader's formula, compiled as C++)
rem   build.bat model          -> only model_test; "plugin" and "fx" likewise
rem   build.bat preview <dir>  -> draws the monitor, the broadcast card and the report into SVG files
rem                               (tools\render_svgs.py makes PNGs)
rem   build.bat shader         -> compiles fx\pulse.hlsl the way the launcher's build does (swofty postfx compile)
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo No se encuentra vswhere.exe: instala Visual Studio con "Desarrollo para el escritorio con C++".
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio no tiene las herramientas de C++ x64 instaladas.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=dll"
if /i "%WHAT%"=="shader" goto shader

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
set FLAGS=/nologo /std:c++latest /EHsc /O2 /MT /W3 /Zc:preprocessor /Zc:__cplusplus /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I include /Fobuild\
rem the addon's version, from the manifest, for the line it logs when it loads
set "PVER=dev"
for /f "tokens=2 delims=:, " %%v in ('findstr /c:"\"version\"" ..\swofty.json') do if "!PVER!"=="dev" set "PVER=%%~v"
set FLAGS=%FLAGS% /DPULSE_VERSION=\"%PVER%\"

if /i "%WHAT%"=="preview" goto preview
set DLL=0
set TESTS=
if /i "%WHAT%"=="dll" set DLL=1
if /i "%WHAT%"=="test" (
    set DLL=1
    set "TESTS=model plugin fx"
)
if /i "%WHAT%"=="model" set "TESTS=model"
if /i "%WHAT%"=="fx" set "TESTS=fx"
if /i "%WHAT%"=="plugin" set "TESTS=plugin"
if "%DLL%"=="0" if "%TESTS%"=="" (
    echo build.bat: unknown target "%WHAT%" ^(dll, test, model, plugin, fx, preview, shader^)
    exit /b 1
)

set "SRC=src\heart.cpp"
if "%DLL%"=="1" set "SRC=!SRC! src\pulse.cpp"
for %%t in (%TESTS%) do set "SRC=!SRC! tests\%%t_test.cpp"
cl %FLAGS% /MP /c %SRC% || exit /b 1
if "%DLL%"=="1" (
    link /NOLOGO /DLL build\pulse.obj build\heart.obj /OUT:pulse.dll /IMPLIB:build\pulse.lib || exit /b 1
    del /q pulse.exp 2>nul
    echo built %CD%\pulse.dll
)
echo %TESTS% | findstr /c:"plugin" >nul && (
    cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB >nul || exit /b 1
)
for %%t in (%TESTS%) do (
    link /NOLOGO build\%%t_test.obj build\heart.obj /OUT:build\%%t_test.exe || exit /b 1
    build\%%t_test.exe || exit /b 1
)
endlocal
exit /b 0

:preview
if "%~2"=="" (
    echo build.bat preview ^<dir^>
    exit /b 1
)
cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB >nul || exit /b 1
cl %FLAGS% /MP /c src\heart.cpp tests\preview.cpp || exit /b 1
link /NOLOGO build\preview.obj build\heart.obj /OUT:build\preview.exe || exit /b 1
build\preview.exe "%~f2" || exit /b 1
endlocal
exit /b 0

:shader
set "SWOFTY=%LOCALAPPDATA%\Programs\SwoftyLauncher\bin\swofty.exe"
if not exist "%SWOFTY%" (
    echo No encuentro swofty.exe en %SWOFTY%
    exit /b 1
)
"%SWOFTY%" postfx compile .. --out build\dxil || exit /b 1
endlocal
exit /b 0
