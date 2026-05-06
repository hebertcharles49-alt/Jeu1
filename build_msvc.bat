@echo off
REM ============================================================
REM Crucible - build via MSVC (cl.exe)
REM Pre-requis :
REM   - Visual Studio Build Tools, ouvre "x64 Native Tools Command Prompt"
REM   - SDL2-devel-VC : https://github.com/libsdl-org/SDL/releases
REM     dezippe puis : set SDL2_DIR=C:\chemin\vers\SDL2-2.30.x
REM ============================================================

setlocal

if not defined SDL2_DIR (
    echo ERREUR: SDL2_DIR non defini.
    echo Telecharge SDL2-devel-VC, dezippe, puis: set SDL2_DIR=C:\chemin\SDL2-2.30.x
    exit /b 1
)

if not exist build mkdir build

set INCS=/I "%SDL2_DIR%\include"
set LIBS=/LIBPATH:"%SDL2_DIR%\lib\x64" SDL2.lib SDL2main.lib shell32.lib

cl /O2 /W3 /nologo %INCS% src\main.c src\world.c src\combat.c src\render.c src\meta.c src\audio.c src\inventory.c src\options.c ^
   /Fobuild\ /Feelement_dungeon.exe /link %LIBS% /SUBSYSTEM:WINDOWS

if errorlevel 1 (
    echo Echec de compilation.
    exit /b 1
)

if exist "%SDL2_DIR%\lib\x64\SDL2.dll" copy "%SDL2_DIR%\lib\x64\SDL2.dll" SDL2.dll >nul
echo OK -- element_dungeon.exe
endlocal
