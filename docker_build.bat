@echo off
setlocal
cd /d "%~dp0"
where docker >nul 2>nul
if errorlevel 1 (
    echo Docker Desktop is required. Start it and retry.
    exit /b 1
)
docker build -t sm64ds-android-builder -f android/Dockerfile .
if errorlevel 1 exit /b 1
docker run --rm -v "%cd%:/project" -v sm64ds-android-gradle:/root/.gradle sm64ds-android-builder
if errorlevel 1 exit /b 1
echo APK: %cd%\android\app\build\outputs\apk\debug\app-debug.apk
