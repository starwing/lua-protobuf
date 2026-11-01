@echo off
rem Build pb.dll on Windows.
rem
rem   build.bat [5.4^|5.5^|jit] [msvc^|mingw] [test]
rem
rem Defaults: Lua 5.5 with MSVC.  The Lua roots below can be overridden with
rem LUA_INC plus LUA_LIB (MSVC) or LUA_INC plus LUA_DLL (MinGW).
setlocal EnableExtensions
cd /d "%~dp0"

set "LUAVER=%~1"
if "%LUAVER%"=="" set "LUAVER=5.5"
set "CC=%~2"
if "%CC%"=="" set "CC=msvc"
set "RUNTEST=%~3"

if /i "%LUAVER%"=="5.5" goto ver55
if /i "%LUAVER%"=="5.4" goto ver54
if /i "%LUAVER%"=="jit" goto verjit
echo usage: build.bat [5.4^|5.5^|jit] [msvc^|mingw] [test] 1>&2
exit /b 1

:ver55
set "LUA_ROOT=C:\Devel\Lua55"
set "LUA_LIBNAME=lua55"
set "LUA_NAME=Lua 5.5"
set "LUA_EXE=%LUA_ROOT%\lua.exe"
set "ARCH=x64"
goto version
:ver54
set "LUA_ROOT=C:\Devel\Lua54"
set "LUA_LIBNAME=lua54"
set "LUA_NAME=Lua 5.4"
set "LUA_EXE=%LUA_ROOT%\lua.exe"
set "ARCH=x64"
goto version
:verjit
set "LUA_ROOT=C:\Devel\LuaJIT"
set "LUA_LIBNAME=lua51"
set "LUA_NAME=LuaJIT"
set "LUA_EXE=%LUA_ROOT%\luajit.exe"
set "ARCH=x86"
goto version

:version
if not defined LUA_INC set "LUA_INC=%LUA_ROOT%\include"
if /i "%CC%"=="mingw" goto mingw
if /i not "%CC%"=="msvc" (
    echo unknown compiler: %CC% 1>&2
    exit /b 1
)
if not defined LUA_LIB set "LUA_LIB=%LUA_ROOT%\lib\%LUA_LIBNAME%.lib"

rem Reconfigure the toolchain for the target arch; a plain ssh session has no
rem Developer Command Prompt, and an x64 prompt is wrong for 32-bit LuaJIT.
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR (
    call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
    if errorlevel 1 exit /b 1
) else (
    where cl >nul 2>&1
    if errorlevel 1 (
        echo cl.exe not found; install the MSVC C++ toolset 1>&2
        exit /b 1
    )
)

echo [msvc] %LUA_NAME%, %ARCH%
cl /nologo /MT /O2 /W3 /DNDEBUG /LD /Fepb.dll /I "%LUA_INC%" /DLUA_BUILD_AS_DLL pb.c "%LUA_LIB%"
if errorlevel 1 exit /b 1
goto tests

:mingw
if /i "%LUAVER%"=="jit" echo note: LuaJIT is 32-bit; needs a 32-bit MinGW
if not defined LUA_DLL set "LUA_DLL=%LUA_ROOT%\%LUA_LIBNAME%.dll"
echo [mingw] %LUA_NAME%, %ARCH%
gcc -O2 -Wall -Wextra -shared -I"%LUA_INC%" pb.c -o pb.dll "%LUA_DLL%"
if errorlevel 1 exit /b 1

:tests
if /i not "%RUNTEST%"=="test" exit /b 0
"%LUA_EXE%" test.lua
if errorlevel 1 exit /b 1
"%LUA_EXE%" protoc_test.lua -x known_issue
exit /b %errorlevel%
