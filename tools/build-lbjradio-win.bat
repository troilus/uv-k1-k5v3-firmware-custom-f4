@echo off
rem Local LBJRadio build for Windows (the CI uses compile-with-docker.sh).
rem
rem Usage:
rem   tools\build-lbjradio-win.bat              English, TX locked (archive build)
rem   tools\build-lbjradio-win.bat -DENABLE_LBJ_TX_LOCK=OFF
rem   tools\build-lbjradio-win.bat -DENABLE_CN_FONT=ON [-DENABLE_LBJ_TX_LOCK=OFF]
rem
rem Needs arm-none-eabi-gcc in PATH (GNU Arm Embedded Toolchain 10-2021.10
rem works), plus cmake and ninja. The GNU Arm 10 linker does not know
rem --no-warn-rwx-segment, which the build adds unconditionally, so the option is
rem stripped from build.ninja after configuring - the CI's newer toolchain does
rem not need that step.
setlocal

set SRC=%~dp0..\
set PRESET=LBJRadio
set EXTRA=%*

if "%EXTRA%"=="" set EXTRA=
set BUILD=%SRC%build\%PRESET%

cmake --preset %PRESET% %EXTRA%
if errorlevel 1 exit /b 1

powershell -NoProfile -Command "$p='%BUILD%\build.ninja'; $t=(Get-Content -Raw $p); $t=$t -replace ' -Wl,--no-warn-rwx-segment',''; Set-Content -NoNewline $p $t"

cmake --build %BUILD% -j
if errorlevel 1 exit /b 1

for %%f in ("%BUILD%\lbjradio.bin") do echo built %%~ff %%~zf bytes
endlocal
