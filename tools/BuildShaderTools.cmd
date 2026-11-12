@echo off
setlocal
cd /d "%~dp0.."
set "DX12_EXTERNAL=%CD%\..\External_Lib"
call :source DLSS https://github.com/NVIDIA/DLSS.git 9981c6350dc58c0ce21f024fd65e31e851c31689
if errorlevel 1 exit /b 1
call :source glslang https://github.com/KhronosGroup/glslang.git ff6ad3c77a2cc2fdea329b4a1d5425c82bc56a1f
if errorlevel 1 exit /b 1
call :source SPIRV-Cross https://github.com/KhronosGroup/SPIRV-Cross.git 57e4efcc84112abcd426dc5ec248b5339bee04f2
if errorlevel 1 exit /b 1
cmake -S "%DX12_EXTERNAL%\src\glslang" -B "%DX12_EXTERNAL%\build\glslang" -G "Visual Studio 17 2022" -A x64 -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_HLSL=OFF -DBUILD_EXTERNAL=OFF -DGLSLANG_ENABLE_INSTALL=OFF
if errorlevel 1 exit /b 1
cmake --build "%DX12_EXTERNAL%\build\glslang" --config Release --target glslang glslang-default-resource-limits --parallel 4
if errorlevel 1 exit /b 1
cmake -S "%DX12_EXTERNAL%\src\SPIRV-Cross" -B "%DX12_EXTERNAL%\build\SPIRV-Cross" -G "Visual Studio 17 2022" -A x64 -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_ENABLE_MSL=OFF -DSPIRV_CROSS_ENABLE_CPP=OFF -DSPIRV_CROSS_ENABLE_C_API=OFF -DSPIRV_CROSS_CLI=OFF
if errorlevel 1 exit /b 1
cmake --build "%DX12_EXTERNAL%\build\SPIRV-Cross" --config Release --target spirv-cross-hlsl --parallel 4
if errorlevel 1 exit /b 1
cmake -S tools\shader-translator -B "%DX12_EXTERNAL%\build\ShaderTranslator" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build "%DX12_EXTERNAL%\build\ShaderTranslator" --config Release --target ShaderTranslator --parallel 4
exit /b %errorlevel%

:source
if not exist "%DX12_EXTERNAL%\src\%~1" (
  git clone --filter=blob:none --no-checkout "%~2" "%DX12_EXTERNAL%\src\%~1"
  if errorlevel 1 exit /b 1
  git -C "%DX12_EXTERNAL%\src\%~1" fetch --depth 1 origin %~3
  if errorlevel 1 exit /b 1
  git -C "%DX12_EXTERNAL%\src\%~1" checkout --detach %~3
  if errorlevel 1 exit /b 1
)
set "DX12_FOUND_REV="
for /f %%R in ('git -C "%DX12_EXTERNAL%\src\%~1" rev-parse HEAD') do set "DX12_FOUND_REV=%%R"
if /i not "%DX12_FOUND_REV%"=="%~3" (
  echo Existing %~1 source differs from the pinned compiler revision. No checkout was changed.
  exit /b 1
)
exit /b 0
