@echo off
rem ==========================================================================
rem  Visual Studio build script (Win32 / x86 Release, static CRT /MT)
rem
rem  Usage: double-click, or run from any command prompt.
rem  The script locates Visual Studio through vswhere.exe and calls vcvarsall x86.
rem  Output: bin\War3Trainer.exe  bin\War3Trainer.dll
rem
rem  NOTE: keep this file pure ASCII (cmd.exe mis-parses UTF-8 / GBK batch files).
rem ==========================================================================
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if not errorlevel 1 goto have_cl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2017 or later with "Desktop development with C++".
    goto fail
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo [ERROR] Visual C++ tools not found.
    goto fail
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 goto fail

:have_cl
if not exist bin mkdir bin
if not exist build\msvc mkdir build\msvc

set CFLAGS=/nologo /O2 /MT /EHsc /utf-8 /W3 /std:c++17 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DWIN32 /DNDEBUG

echo [1/3] War3Trainer.dll
cl %CFLAGS% /Iinclude /Isrc\dll /LD /Fobuild\msvc\ /Febin\War3Trainer.dll src\dll\*.cpp /link /MACHINE:X86 version.lib user32.lib
if errorlevel 1 goto fail

echo [2/3] resources
pushd res
rc /nologo /I..\src\gui /fo ..\build\msvc\app.res app.rc
if errorlevel 1 ( popd & goto fail )
popd

echo [3/3] War3Trainer.exe
cl %CFLAGS% /Isrc\gui /Fobuild\msvc\ /Febin\War3Trainer.exe src\gui\*.cpp build\msvc\app.res /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib user32.lib gdi32.lib advapi32.lib
if errorlevel 1 goto fail

del /q bin\*.exp bin\*.lib 2>nul
echo.
echo Done: bin\War3Trainer.exe  bin\War3Trainer.dll
pause
exit /b 0

:fail
echo.
echo Build failed.
pause
exit /b 1
