# WeighMyBru² Enhanced Build & Release Script (PowerShell)
# This script builds firmware and automatically updates website releases

param(
    [switch]$Clean,
    [switch]$Release,
    [string]$Version,
    [string]$BuildNumber,
    [string]$Output = "build-output"
)

$ErrorActionPreference = "Stop"

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "    WeighMyBru² Enhanced Build & Release" -ForegroundColor Cyan  
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildOutputDir = Join-Path $ScriptDir $Output
$WebsiteReleasesDir = Join-Path $ScriptDir "website\releases"
$LatestDir = Join-Path $WebsiteReleasesDir "latest"

# Step 1: Run the existing build script
Write-Host "[STEP 1] Running main build script..." -ForegroundColor Yellow

$buildArgs = @()
if ($Clean) { $buildArgs += "--clean" }
if ($Release) { $buildArgs += "--release" }
if ($Version) { $buildArgs += "--version", $Version }
if ($BuildNumber) { $buildArgs += "--build-number", $BuildNumber }
if ($Output -ne "build-output") { $buildArgs += "--output", $Output }

$buildScript = Join-Path $ScriptDir "build.bat"
$process = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "`"$buildScript`"", $buildArgs -Wait -PassThru -NoNewWindow

if ($process.ExitCode -ne 0) {
    Write-Error "Build failed with exit code $($process.ExitCode)"
}

# Step 2: Extract version from Version.h
Write-Host "[STEP 2] Extracting version information..." -ForegroundColor Yellow

$versionFile = Join-Path $ScriptDir "include\Version.h"
$versionContent = Get-Content $versionFile

$versionMajor = ($versionContent | Select-String "WEIGHMYBRU_VERSION_MAJOR\s+(\d+)" | ForEach-Object { $_.Matches[0].Groups[1].Value })
$versionMinor = ($versionContent | Select-String "WEIGHMYBRU_VERSION_MINOR\s+(\d+)" | ForEach-Object { $_.Matches[0].Groups[1].Value })
$versionPatch = ($versionContent | Select-String "WEIGHMYBRU_VERSION_PATCH\s+(\d+)" | ForEach-Object { $_.Matches[0].Groups[1].Value })

$currentVersion = "$versionMajor.$versionMinor.$versionPatch"

Write-Host "[INFO] Current version: $currentVersion" -ForegroundColor Green

# Step 3: Create website release directories
Write-Host "[STEP 3] Setting up website release directories..." -ForegroundColor Yellow

if (-not (Test-Path $WebsiteReleasesDir)) { New-Item -ItemType Directory -Path $WebsiteReleasesDir -Force }
if (-not (Test-Path $LatestDir)) { New-Item -ItemType Directory -Path $LatestDir -Force }

$versionDir = Join-Path $WebsiteReleasesDir "v$currentVersion"
if (-not (Test-Path $versionDir)) { New-Item -ItemType Directory -Path $versionDir -Force }

# Step 4: Copy manifest and firmware files
Write-Host "[STEP 4] Copying release files to website..." -ForegroundColor Yellow

$buildLatestDir = Join-Path $BuildOutputDir "latest"

# Copy manifest files to latest
Copy-Item -Path (Join-Path $buildLatestDir "manifest-supermini.json") -Destination $LatestDir -Force
Copy-Item -Path (Join-Path $buildLatestDir "manifest-xiao.json") -Destination $LatestDir -Force

# Copy firmware binaries to latest
Get-ChildItem -Path $buildLatestDir -Filter "*.bin" | Copy-Item -Destination $LatestDir -Force

# Copy all files to version-specific directory
Get-ChildItem -Path $LatestDir | Copy-Item -Destination $versionDir -Force

Write-Host "[INFO] Files copied to:" -ForegroundColor Green
Write-Host "  - $LatestDir"
Write-Host "  - $versionDir"

# Step 5: Update releases index
Write-Host "[STEP 5] Updating releases index..." -ForegroundColor Yellow

$releaseDate = Get-Date -Format "yyyy-MM-dd"

# Create releases index
$indexData = @{
    latest = $currentVersion
    releases = @(
        @{
            version = $currentVersion
            date = $releaseDate
            supermini_manifest = "./v$currentVersion/manifest-supermini.json"
            xiao_manifest = "./v$currentVersion/manifest-xiao.json"
        }
    )
}

$indexPath = Join-Path $WebsiteReleasesDir "index.json"
$indexData | ConvertTo-Json -Depth 3 | Out-File -FilePath $indexPath -Encoding UTF8

# Step 6: Check if we should auto-commit
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "       Release Preparation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "Version: $currentVersion"
Write-Host "Release Date: $releaseDate"
Write-Host ""
Write-Host "Website files updated:" -ForegroundColor Green
Write-Host "  ✓ $LatestDir\manifest-*.json"
Write-Host "  ✓ $LatestDir\*.bin"  
Write-Host "  ✓ $versionDir\*"
Write-Host "  ✓ $indexPath"
Write-Host ""

# Ask if user wants to auto-commit
$autoCommit = Read-Host "Would you like to automatically commit and push these changes? (y/N)"

if ($autoCommit -eq 'y' -or $autoCommit -eq 'Y') {
    Write-Host ""
    Write-Host "[AUTO-COMMIT] Adding files to git..." -ForegroundColor Yellow
    
    git add "website/releases/"
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[AUTO-COMMIT] Committing changes..." -ForegroundColor Yellow
        git commit -m "Release v$currentVersion - Updated firmware manifests and binaries"
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[AUTO-COMMIT] Pushing to GitHub..." -ForegroundColor Yellow
            git push
            
            if ($LASTEXITCODE -eq 0) {
                Write-Host ""
                Write-Host "🚀 SUCCESS! Your changes have been pushed to GitHub." -ForegroundColor Green
                Write-Host "Cloudflare will automatically deploy the new version within a few minutes." -ForegroundColor Green
            } else {
                Write-Warning "Push failed. Please check your git configuration and push manually."
            }
        } else {
            Write-Warning "Commit failed. Please check git status and commit manually."
        }
    } else {
        Write-Warning "Git add failed. Please check the file paths and add manually."
    }
} else {
    Write-Host ""
    Write-Host "[NEXT STEPS] Manual deployment:" -ForegroundColor Yellow
    Write-Host "1. Review the changes:"
    Write-Host "   git status"
    Write-Host ""
    Write-Host "2. Commit and push to deploy:"
    Write-Host "   git add website/releases/"
    Write-Host "   git commit -m `"Release v$currentVersion - Updated firmware manifests`""
    Write-Host "   git push"
    Write-Host ""
    Write-Host "3. Your Cloudflare site will auto-update with the new version!"
}

Write-Host ""