@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
where link
link /nologo /OUT:build\current\checks\PhysicsChecks.exe build\current\checks\*.obj /LIBPATH:..\External_Lib\lib glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
build\current\checks\PhysicsChecks.exe
exit /b %errorlevel%
