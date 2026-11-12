@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b 1
if not exist "build\current\checks" mkdir "build\current\checks"
cl /nologo /EHsc /std:c++20 /MDd /I src /I "..\External_Lib\include" tests\SimulationChecks.cpp src\QuantumStates.cpp src\CameraMotion.cpp src\StarSystem.cpp src\SolarSystem.cpp src\StellarActivity.cpp src\UniverseCatalog.cpp src\InterstellarWorlds.cpp src\KerrSpacetime.cpp src\OrbitalMechanics.cpp src\PlanetTerrain.cpp src\Atmosphere.cpp src\SaturnRings.cpp src\glad.c /Fo"build\current\checks\\" /Fe"build\current\checks\SimulationChecks.exe" /link /LIBPATH:"..\External_Lib\lib" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
"build\current\checks\SimulationChecks.exe"
exit /b %errorlevel%
