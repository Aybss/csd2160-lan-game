@echo off
echo ============================================
echo  TankNet - Auto Setup
echo ============================================

:: Check if vcpkg already exists
if exist "C:\vcpkg\vcpkg.exe" (
    echo [OK] vcpkg already installed at C:\vcpkg
    goto BUILD
)

echo [1/3] Cloning vcpkg...
git clone https://github.com/microsoft/vcpkg C:\vcpkg
if errorlevel 1 (
    echo [ERROR] git clone failed. Make sure Git is installed.
    pause & exit /b 1
)

echo [2/3] Bootstrapping vcpkg...
call C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
if errorlevel 1 ( echo [ERROR] Bootstrap failed. & pause & exit /b 1 )

echo [3/3] Integrating vcpkg with Visual Studio...
C:\vcpkg\vcpkg integrate install
if errorlevel 1 ( echo [ERROR] Integrate failed. & pause & exit /b 1 )

:BUILD
echo.
echo [Building] Configuring CMake... (SFML + Opus will download automatically)
cmake --preset windows-x64-release
if errorlevel 1 ( echo [ERROR] CMake configure failed. & pause & exit /b 1 )

echo [Building] Compiling...
cmake --build build --preset release
if errorlevel 1 ( echo [ERROR] Build failed. & pause & exit /b 1 )

echo.
echo ============================================
echo  Build complete! Run: cd build\Release
echo  Then: run.bat  (same machine test)
echo  Or:   TankNet.exe server / client
echo ============================================
pause
