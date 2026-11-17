@echo off
rem RetroCRT - compila con MSVC (preferido) o MinGW-w64. No necesita CMake.
rem   bin\RetroCRT.exe           portable (Inicio y paneles del sistema se ven sin filtro)
rem   bin\RetroCRT_UIAccess.exe  para install-uiaccess.ps1 (filtra tambien Inicio y paneles)
rem Uso: build.bat            (pausa al terminar si se abre con doble clic)
rem      build.bat nopause
setlocal EnableExtensions
cd /d "%~dp0"

set "OUT=bin"
set "OBJ=build\obj"
set "CPPOBJ=build\obj\cpp"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%CPPOBJ%" mkdir "%CPPOBJ%"

rem 1) MSVC ya en el PATH (Developer Command Prompt)
where cl >nul 2>nul && goto :msvc

rem 2) Visual Studio / Build Tools instalados: localizar con vswhere y cargar vcvars64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto :msvc
)

rem 3) MinGW-w64 (MSYS2 UCRT64, w64devkit, llvm-mingw...) en el PATH
where g++ >nul 2>nul && goto :mingw

echo.
echo [ERROR] No se encontro ningun compilador de C++.
echo   Opcion A: winget install MartinStorsjo.LLVM-MinGW.UCRT   (y abre una consola nueva)
echo   Opcion B: winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
goto :fail

:msvc
echo [RetroCRT] Compilando con MSVC...
set "LIBS=d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib"
rc /nologo /i "%CD%" /i "%CD%\src" /fo "%OBJ%\RetroCRT.res" src\RetroCRT.rc || goto :fail
rc /nologo /i "%CD%" /i "%CD%\src" /d RETROCRT_UIACCESS /fo "%OBJ%\RetroCRT_UIAccess.res" src\RetroCRT.rc || goto :fail
cl /nologo /c /std:c++17 /O2 /EHsc /W4 /utf-8 /permissive- /MT ^
   /DUNICODE /D_UNICODE /DNDEBUG /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
   /Fo%CPPOBJ%\ src\*.cpp || goto :fail
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"%OUT%\RetroCRT.exe" %CPPOBJ%\*.obj "%OBJ%\RetroCRT.res" %LIBS% || goto :fail
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"%OUT%\RetroCRT_UIAccess.exe" %CPPOBJ%\*.obj "%OBJ%\RetroCRT_UIAccess.res" %LIBS% || goto :fail
goto :ok

:mingw
echo [RetroCRT] Compilando con MinGW-w64...
where windres >nul 2>nul || (echo [ERROR] windres no esta en el PATH. & goto :fail)
set "CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -municode -DUNICODE -D_UNICODE -DNDEBUG -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
set "LDFLAGS=-municode -mwindows -static -static-libgcc -static-libstdc++ -ld3d11 -ldxgi -lcomctl32 -lshell32 -lgdi32 -luser32 -lole32"
windres -I . -I src -O coff -o "%OBJ%/RetroCRT.res" src/RetroCRT.rc || goto :fail
windres -I . -I src -DRETROCRT_UIACCESS -O coff -o "%OBJ%/RetroCRT_UIAccess.res" src/RetroCRT.rc || goto :fail
for %%f in (src\*.cpp) do (
    g++ %CXXFLAGS% -c "%%f" -o "%CPPOBJ%\%%~nf.o" || goto :fail
)
g++ %CPPOBJ%/*.o "%OBJ%/RetroCRT.res" -o "%OUT%/RetroCRT.exe" %LDFLAGS% || goto :fail
g++ %CPPOBJ%/*.o "%OBJ%/RetroCRT_UIAccess.res" -o "%OUT%/RetroCRT_UIAccess.exe" %LDFLAGS% || goto :fail
goto :ok

:ok
echo.
echo [OK] %OUT%\RetroCRT.exe           (portable)
echo [OK] %OUT%\RetroCRT_UIAccess.exe  (instalar con install-uiaccess.ps1)
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo [ERROR] La compilacion fallo.
if /i not "%~1"=="nopause" pause
exit /b 1
