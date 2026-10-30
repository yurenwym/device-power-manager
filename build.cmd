@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
  if not defined VSINSTALL (
    echo Visual Studio C++ Build Tools not found.
    exit /b 1
  )
  call "!VSINSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
)
if not exist build mkdir build
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /O1 /MT /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Fo:build\ /Fe:bin\ProtocolTests.exe tests\protocol_tests.cpp /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
bin\ProtocolTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /O1 /MT /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Fo:build\ /Fe:bin\IoTests.exe tests\io_tests.cpp src\hid.cpp /link /INCREMENTAL:NO setupapi.lib hid.lib ole32.lib
if errorlevel 1 exit /b 1
bin\IoTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /O1 /MT /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Fo:build\ /Fe:bin\BatteryProbe.exe src\probe.cpp src\hid.cpp src\adapters.cpp /link /INCREMENTAL:NO setupapi.lib hid.lib ole32.lib
if errorlevel 1 exit /b 1
if exist src\app.cpp (
  rc /nologo /c65001 /I assets /fo build\app.res src\app.rc
  if errorlevel 1 exit /b 1
  cl /nologo /std:c++17 /EHsc /utf-8 /W4 /O1 /MT /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Fo:build\ /Fe:bin\设备电源管理.exe src\app.cpp src\settings.cpp src\hid.cpp src\adapters.cpp build\app.res /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest setupapi.lib hid.lib ole32.lib shell32.lib user32.lib gdi32.lib comctl32.lib uuid.lib advapi32.lib
  if errorlevel 1 exit /b 1
)
echo Build succeeded.
