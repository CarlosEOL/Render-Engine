@echo off
setlocal
cd /d "%~dp0.."
cmake -S tests\kerr -B build\current\kerr-cpu -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build\current\kerr-cpu --config Release --parallel 4
if errorlevel 1 exit /b 1
build\current\kerr-cpu\Release\KerrObserverChecks.exe
exit /b %errorlevel%
