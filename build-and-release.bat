@echo off
setlocal enabledelayedexpansion

rem WeighMyBru² Enhanced Build & Release Script
rem This script builds firmware and automatically updates website releases

set "SCRIPT_DIR=%~dp0"
set "BUILD_OUTPUT_DIR=build-output"
set "WEBSITE_RELEASES_DIR=website\releases"
set "LATEST_DIR=%WEBSITE_RELEASES_DIR%\latest"

echo ===============================================
echo    WeighMyBru² Enhanced Build & Release
echo ===============================================
echo.

rem Step 1: Run the existing build script
echo [STEP 1] Running main build script...
call "%SCRIPT_DIR%build.bat" %*
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

rem Step 2: Extract version from Version.h
echo [STEP 2] Extracting version information...

rem Read version from Version.h
for /f "tokens=3" %%i in ('findstr "WEIGHMYBRU_VERSION_MAJOR" "%SCRIPT_DIR%include\Version.h"') do set "VERSION_MAJOR=%%i"
for /f "tokens=3" %%i in ('findstr "WEIGHMYBRU_VERSION_MINOR" "%SCRIPT_DIR%include\Version.h"') do set "VERSION_MINOR=%%i"
for /f "tokens=3" %%i in ('findstr "WEIGHMYBRU_VERSION_PATCH" "%SCRIPT_DIR%include\Version.h"') do set "VERSION_PATCH=%%i"

rem Construct version string
set "CURRENT_VERSION=%VERSION_MAJOR%.%VERSION_MINOR%.%VERSION_PATCH%"

echo [INFO] Current version: %CURRENT_VERSION%

rem Step 3: Create website release directories
echo [STEP 3] Setting up website release directories...

if not exist "%WEBSITE_RELEASES_DIR%" mkdir "%WEBSITE_RELEASES_DIR%"
if not exist "%LATEST_DIR%" mkdir "%LATEST_DIR%"

set "VERSION_DIR=%WEBSITE_RELEASES_DIR%\v%CURRENT_VERSION%"
if not exist "%VERSION_DIR%" mkdir "%VERSION_DIR%"

rem Step 4: Copy manifest and firmware files
echo [STEP 4] Copying release files to website...

rem Copy manifest files to latest
copy "%BUILD_OUTPUT_DIR%\latest\manifest-supermini.json" "%LATEST_DIR%\" >nul
copy "%BUILD_OUTPUT_DIR%\latest\manifest-xiao.json" "%LATEST_DIR%\" >nul

rem Copy firmware binaries to latest
copy "%BUILD_OUTPUT_DIR%\latest\*.bin" "%LATEST_DIR%\" >nul

rem Copy all files to version-specific directory
copy "%LATEST_DIR%\*" "%VERSION_DIR%\" >nul

echo [INFO] Files copied to:
echo   - %LATEST_DIR%
echo   - %VERSION_DIR%

rem Step 5: Update releases index
echo [STEP 5] Updating releases index...

rem Get current date
for /f "tokens=*" %%i in ('powershell -command "Get-Date -Format 'yyyy-MM-dd'"') do set "RELEASE_DATE=%%i"

rem Create or update index.json
echo {> "%WEBSITE_RELEASES_DIR%\index.json"
echo   "latest": "%CURRENT_VERSION%",>> "%WEBSITE_RELEASES_DIR%\index.json"
echo   "releases": [>> "%WEBSITE_RELEASES_DIR%\index.json"
echo     {>> "%WEBSITE_RELEASES_DIR%\index.json"
echo       "version": "%CURRENT_VERSION%",>> "%WEBSITE_RELEASES_DIR%\index.json"
echo       "date": "%RELEASE_DATE%",>> "%WEBSITE_RELEASES_DIR%\index.json"
echo       "supermini_manifest": "./v%CURRENT_VERSION%/manifest-supermini.json",>> "%WEBSITE_RELEASES_DIR%\index.json"
echo       "xiao_manifest": "./v%CURRENT_VERSION%/manifest-xiao.json">> "%WEBSITE_RELEASES_DIR%\index.json"
echo     }>> "%WEBSITE_RELEASES_DIR%\index.json"
echo   ]>> "%WEBSITE_RELEASES_DIR%\index.json"
echo }>> "%WEBSITE_RELEASES_DIR%\index.json"

rem Step 6: Show summary and next steps
echo.
echo ========================================
echo       Release Preparation Complete!
echo ========================================
echo Version: %CURRENT_VERSION%
echo Release Date: %RELEASE_DATE%
echo.
echo Website files updated:
echo   ✓ %LATEST_DIR%\manifest-*.json
echo   ✓ %LATEST_DIR%\*.bin  
echo   ✓ %VERSION_DIR%\*
echo   ✓ %WEBSITE_RELEASES_DIR%\index.json
echo.
echo [NEXT STEPS]
echo 1. Review the changes:
echo    git status
echo.
echo 2. Commit and push to deploy:
echo    git add website/releases/
echo    git commit -m "Release v%CURRENT_VERSION% - Updated firmware manifests"
echo    git push
echo.
echo 3. Your Cloudflare site will auto-update with the new version!
echo.

endlocal