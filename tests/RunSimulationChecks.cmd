@echo off
setlocal
cd /d "%~dp0.."
if not defined EXTERNAL_LIB set "EXTERNAL_LIB=..\External_Lib"
where cl >nul 2>nul && goto build
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (echo Visual Studio C++ tools not found & exit /b 1)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
if errorlevel 1 exit /b 1
:build
if not exist "build\checks" mkdir "build\checks"
cl /nologo /EHsc /std:c++20 /MDd /W3 /I src /I "%EXTERNAL_LIB%\include" tests\SimulationChecks.cpp src\QuantumStates.cpp src\StarSystem.cpp src\SolarSystem.cpp src\OrbitalMechanics.cpp src\PlanetTerrain.cpp src\Atmosphere.cpp src\glad.c /Fo"build\checks\\" /Fe"build\checks\SimulationChecks.exe" /link /LIBPATH:"%EXTERNAL_LIB%\lib" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
"build\checks\SimulationChecks.exe"
exit /b %errorlevel%
