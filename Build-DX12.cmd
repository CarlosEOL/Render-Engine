@echo off
setlocal
cd /d "%~dp0"
set "DX12_CONFIGURATION=%~1"
if not defined DX12_CONFIGURATION set "DX12_CONFIGURATION=Debug"
if /i not "%DX12_CONFIGURATION%"=="Debug" if /i not "%DX12_CONFIGURATION%"=="Release" (
  echo Usage: Build-DX12.cmd [Debug^|Release]
  exit /b 2
)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b 1
MSBuild Render-Engine.DX12.vcxproj /t:Build /p:Configuration=%DX12_CONFIGURATION% /p:Platform=x64 /nologo /verbosity:minimal
exit /b %errorlevel%
