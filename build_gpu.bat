@echo off
rem ---- locate MSVC (Visual Studio / Build Tools with the C++ workload) ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio C++ build tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
rem ---- fetch dependencies on first build ----
if not exist deps mkdir deps
if not exist deps\glfw-3.4.bin.WIN64 (
  curl -sSL -o deps\glfw.zip https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip && tar -xf deps\glfw.zip -C deps && del deps\glfw.zip
)
if not exist deps\glew-2.2.0 (
  curl -sSL -o deps\glew.zip https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0-win32.zip && tar -xf deps\glew.zip -C deps && del deps\glew.zip
)
if not exist deps\glm\glm (
  mkdir deps\glm 2>nul
  curl -sSL -o deps\glm.zip https://github.com/g-truc/glm/releases/download/1.0.1/glm-1.0.1-light.zip && tar -xf deps\glm.zip -C deps\glm && del deps\glm.zip
)
if not exist deps\imgui (
  curl -sSL -o deps\imgui.zip https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip && tar -xf deps\imgui.zip -C deps && ren deps\imgui-1.91.8 imgui && del deps\imgui.zip
)
if not exist deps\stb\stb_image_write.h (
  mkdir deps\stb 2>nul
  curl -sSL -o deps\stb\stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
)
if not exist deps\miniaudio\miniaudio.h (
  mkdir deps\miniaudio 2>nul
  curl -sSL -o deps\miniaudio\miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h
)
if not exist build_gpu mkdir build_gpu
set IMGUI=deps\imgui
cl /nologo /EHsc /O2 /MD /std:c++17 /utf-8 /DGLEW_STATIC /MP ^
  /I deps\glfw-3.4.bin.WIN64\include /I deps\glew-2.2.0\include /I deps\glm ^
  /I %IMGUI% /I %IMGUI%\backends /I deps\stb /I deps\miniaudio ^
  black_hole.cpp miniaudio_impl.cpp %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
  %IMGUI%\backends\imgui_impl_glfw.cpp %IMGUI%\backends\imgui_impl_opengl3.cpp ^
  /Fobuild_gpu\ /Febuild_gpu\BlackHole3D_GPU.exe ^
  /link /NODEFAULTLIB:LIBCMT /LIBPATH:deps\glfw-3.4.bin.WIN64\lib-vc2022 /LIBPATH:deps\glew-2.2.0\lib\Release\x64 ^
  glfw3.lib glew32s.lib opengl32.lib gdi32.lib user32.lib shell32.lib
if errorlevel 1 exit /b 1
copy /y *.comp build_gpu\ >nul
copy /y *.vert build_gpu\ >nul
copy /y *.frag build_gpu\ >nul
for %%f in (music.mp3 music.wav music.flac) do if exist %%f copy /y %%f build_gpu\ >nul
del /q build_gpu\*.obj build_gpu\*.lib build_gpu\*.exp 2>nul
echo BUILD OK
