@echo off
REM ============================================================
REM  Crucible - script de build Windows (MinGW)
REM
REM  Pre-requis :
REM    - MSYS2 + SDL2 (option recommandee, voir Readme.txt)
REM    OU
REM    - SDL2-devel-mingw avec SDL2_DIR pointant dessus
REM ============================================================

setlocal

if not exist build mkdir build

set CFLAGS=-O2 -Wall -Wextra -std=c99
set SOURCES=src\main.c src\world.c src\combat.c src\render.c src\meta.c src\audio.c src\inventory.c src\options.c

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
        echo  - Installe MSYS2 + SDL2 (voir Readme.txt), ou
        echo  - definis SDL2_DIR.
        exit /b 1
    )
)

echo Compilation...
gcc %CFLAGS% %SDL_CFLAGS% %SOURCES% %SDL_LIBS% -lm -mwindows -o element_dungeon.exe
if errorlevel 1 (
    echo Echec de compilation.
    exit /b 1
)

if defined SDL2_DIR (
    if exist "%SDL2_DIR%\bin\SDL2.dll" copy "%SDL2_DIR%\bin\SDL2.dll" SDL2.dll >nul
)

echo OK -- lance element_dungeon.exe ou tape: element_dungeon.exe
endlocal
