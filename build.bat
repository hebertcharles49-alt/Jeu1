@echo off
REM ----------------------------------------------------------------
REM Crucible - script de build Windows
REM
REM Pre-requis : MinGW-w64 + SDL2
REM   Option 1 : MSYS2  (https://www.msys2.org)
REM     pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config
REM     ouvre "MSYS2 MinGW 64-bit" puis :  ./build.bat
REM
REM   Option 2 : SDL2 dev archive officielle
REM     telecharge SDL2-devel-2.30.x-mingw.tar.gz sur libsdl.org
REM     pose le dossier x86_64-w64-mingw32 a cote, puis :
REM       set SDL2_DIR=C:\chemin\vers\SDL2-2.30.x\x86_64-w64-mingw32
REM       build.bat
REM ----------------------------------------------------------------

setlocal

if not exist build mkdir build

set CFLAGS=-O2 -Wall -Wextra -std=c99
set SOURCES=src\main.c src\world.c src\combat.c src\render.c src\meta.c

REM Detection SDL2
set SDL_CFLAGS=
set SDL_LIBS=

where sdl2-config >nul 2>&1
if %errorlevel%==0 (
    for /f "delims=" %%i in ('sdl2-config --cflags') do set SDL_CFLAGS=%%i
    for /f "delims=" %%i in ('sdl2-config --libs')   do set SDL_LIBS=%%i
) else (
    if defined SDL2_DIR (
        set SDL_CFLAGS=-I"%SDL2_DIR%\include\SDL2" -Dmain=SDL_main
        set SDL_LIBS=-L"%SDL2_DIR%\lib" -lmingw32 -lSDL2main -lSDL2
    ) else (
        echo ERREUR: SDL2 introuvable.
        echo  - Installe MSYS2 + SDL2, ou
        echo  - Telecharge SDL2-devel-mingw et pose SDL2_DIR.
        exit /b 1
    )
)

echo Compilation...
gcc %CFLAGS% %SDL_CFLAGS% %SOURCES% %SDL_LIBS% -lm -mwindows -o crucible.exe
if errorlevel 1 (
    echo Echec de compilation.
    exit /b 1
)

REM Si SDL2.dll est dans %SDL2_DIR%\bin, on le copie a cote
if defined SDL2_DIR (
    if exist "%SDL2_DIR%\bin\SDL2.dll" copy "%SDL2_DIR%\bin\SDL2.dll" SDL2.dll >nul
)

echo OK -- ./crucible.exe
endlocal
