@echo off
rem Dye All build: MSVC Build Tools 2019 + the CMake and Ninja they bundle.
rem   build.bat         configure and build Release into build\, copy DyeAll.asi to ..\dist\
rem   build.bat clean   wipe build\ first
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
set "VCVARS=%VS%\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "HERE=%~dp0"
if /i "%~1"=="clean" if exist "%HERE%build" rmdir /s /q "%HERE%build"
call "%VCVARS%" >nul 2>&1
if errorlevel 1 ( echo vcvars64.bat not found & exit /b 1 )
"%CMAKE%" -S "%HERE%." -B "%HERE%build" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%HERE%build"
if errorlevel 1 exit /b 1
endlocal
