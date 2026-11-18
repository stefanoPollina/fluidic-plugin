@echo off
rem Builds FLUIDIC on your PC. Needs: Visual Studio 2022 (Community is free) with "Desktop development with C++",
rem which includes CMake. Run this from "Developer PowerShell / Command Prompt for VS 2022", or just double-click it.
setlocal
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build --config Release --target FLUIDIC_VST3 FLUIDIC_Standalone --parallel || goto :fail
echo.
echo Built:
echo   build\FLUIDIC_artefacts\Release\VST3\FLUIDIC.vst3
echo   build\FLUIDIC_artefacts\Release\Standalone\FLUIDIC.exe
echo.
echo To install the VST3, copy FLUIDIC.vst3 into C:\Program Files\Common Files\VST3
echo (or run install-vst3.bat as administrator), then rescan plugins in your DAW.
pause
exit /b 0
:fail
echo Build failed - see the messages above.
pause
exit /b 1
