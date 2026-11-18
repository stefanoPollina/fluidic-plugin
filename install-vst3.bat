@echo off
rem Copies the freshly built FLUIDIC.vst3 and FLUIDIC FX.vst3 into the standard VST3 folder. Right-click → Run as administrator.
setlocal
cd /d "%~dp0"
set SRC=build\FLUIDIC_artefacts\Release\VST3\FLUIDIC.vst3
set DST=%CommonProgramFiles%\VST3\FLUIDIC.vst3
if not exist "%SRC%" ( echo Build first: run build-windows.bat & pause & exit /b 1 )
robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NJS >nul
if %ERRORLEVEL% GEQ 8 ( echo Copy failed - run this as administrator. & pause & exit /b 1 )
set SRCFX=build\FLUIDIC_FX_artefacts\Release\VST3\FLUIDIC FX.vst3
set DSTFX=%CommonProgramFiles%\VST3\FLUIDIC FX.vst3
if exist "%SRCFX%" robocopy "%SRCFX%" "%DSTFX%" /E /NFL /NDL /NJH /NJS >nul
echo Installed FLUIDIC and FLUIDIC FX to %CommonProgramFiles%\VST3. Rescan plugins in your DAW.
pause
