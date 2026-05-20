@echo off
REM ============================================================
REM  build_installer.bat -- compile le jeu puis l'installer.
REM  A lancer depuis "MSYS2 MinGW 64-bit" OU un cmd avec mingw64
REM  dans le PATH + Inno Setup installe (chemin par defaut).
REM ============================================================
setlocal
cd /d "%~dp0\.."

echo === [1/3] Build du jeu (make) ===
make
if errorlevel 1 (
    echo Build echoue. Verifie MSYS2 / SDL2 / Makefile.
    exit /b 1
)

echo === [2/3] Copie SDL2.dll a cote de l'exe ===
if not exist "SDL2.dll" (
    if exist "C:\msys64\mingw64\bin\SDL2.dll" (
        copy /Y "C:\msys64\mingw64\bin\SDL2.dll" "SDL2.dll" >nul
    ) else (
        echo SDL2.dll introuvable. Copie le manuellement a la racine.
        exit /b 1
    )
)

echo === [3/3] Compile l'installer Inno Setup ===
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup introuvable. Installe-le depuis :
    echo   https://jrsoftware.org/isdl.php
    exit /b 1
)
"%ISCC%" "installer\element_dungeon.iss"
if errorlevel 1 (
    echo ISCC a echoue.
    exit /b 1
)
echo.
echo === OK : installer\out\ElementDungeon-Setup-*.exe pret ===
endlocal
