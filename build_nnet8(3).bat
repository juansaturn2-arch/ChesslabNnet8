@echo off
rem ==========================================================================
rem  ChesslabNnet8 - build the exe with the six nets and the book built in
rem
rem  Put this .bat, ChesslabNnet8.c, the six k7net*.k7nn files and
rem  k7Opennings.bin together in one folder (D:\CLCoreGCC\ChesslabNet8)
rem  and double-click it. Uses MSYS2 MinGW64 gcc.
rem ==========================================================================
setlocal
cd /d "%~dp0"

where gcc >nul 2>nul || set "PATH=D:\mingw64\bin;%PATH%"
where gcc >nul 2>nul || (
    echo gcc not found. Install MSYS2 MinGW64 or add its bin folder to PATH.
    pause
    exit /b 1
)

set MISSING=
for %%f in (ChesslabNnet8.c k7net21k.k7nn k7net22k.k7nn k7net23k.k7nn k7net24k.k7nn k7net25k.k7nn k7net31k.k7nn k7Opennings.bin) do (
    if not exist "%%f" (
        echo   missing: %%f
        set MISSING=1
    )
)
if defined MISSING (
    echo.
    echo Every file above must be in %CD% before building.
    echo ^(A different file name? Edit N8_NET1..N8_NET6 / N8_BOOK near the top of the "built-in files" part of ChesslabNnet8.c^)
    pause
    exit /b 1
)

echo Building ChesslabNnet8.exe ...
gcc -O3 -march=native -static -o ChesslabNnet8.exe ChesslabNnet8.c -lm
if errorlevel 1 goto :fail

echo.
echo Done: ChesslabNnet8.exe
echo   Brain vs brain in Arena: tick SelfPlay, pick Brain (White) and
echo   SelfPlayOpponent (Black), then play ChesslabNnet8 vs ChesslabNnet8.
echo.
echo Quick check (perft 5 must be 4865609):
(echo perft 5& echo brains& echo quit) | ChesslabNnet8.exe
echo.
pause
exit /b 0

:fail
echo Build failed - see the messages above.
pause
exit /b 1
