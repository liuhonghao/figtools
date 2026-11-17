# Package the figtools desktop workbench as a distributable folder + zip.
# Run from any directory: powershell -File scripts/package_windows.ps1
# Everything written stays under D:\figtools\dist (never in personal folders).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "build\gui"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "figtools-1.5.0-win64"
$zip = Join-Path $dist "figtools-1.5.0-win64.zip"

if (-not (Test-Path (Join-Path $source "figtools_gui.exe"))) {
    Write-Error "Desktop binary not found. Run scripts/build_gui.sh first."
    exit 1
}

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# Ship only what the app needs: binary, Qt/MinGW runtime DLLs, the Windows
# platform plugin and the Python worker. Leave moc sources, object files and
# debug leftovers in the build directory.
Copy-Item -Force (Join-Path $source "figtools_gui.exe") $stage
foreach ($dll in Get-ChildItem $source -Filter "*.dll" -File) {
    Copy-Item -Force $dll.FullName $stage
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage "platforms") | Out-Null
Copy-Item -Force (Join-Path $source "platforms\qwindows.dll") (Join-Path $stage "platforms")
Copy-Item -Recurse -Force (Join-Path $source "python") (Join-Path $stage "python")

foreach ($cache in Get-ChildItem -Recurse -Directory -Filter "__pycache__" -Path (Join-Path $stage "python")) {
    Remove-Item -Recurse -Force -LiteralPath $cache.FullName
}

$launcher = @'
@echo off
cd /d "%~dp0"
start "" "figtools_gui.exe"
'@
Set-Content -Path (Join-Path $stage "run_figtools.bat") -Value $launcher -Encoding Ascii

$readme = @'
figtools 1.5.0 - image workbench

Run: double-click run_figtools.bat
Or:  figtools_gui.exe

The folder is self-contained for image editing: PNG, BMP, PPM, JPEG, WebP and
TGA are supported. Training and inference need Python with torch; the app finds
a torch-capable conda environment automatically, or set FIGTOOLS_PYTHON.

Model training/inference scripts live in python\ next to the executable.
Ultralytics YOLO is optional and licensed AGPL-3.0; install it separately only
if you need YOLO training. It is not bundled with this application.
'@
Set-Content -Path (Join-Path $stage "README.txt") -Value $readme -Encoding Ascii

if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

"package: $zip"
"size: {0:N1} MB" -f ((Get-Item $zip).Length / 1MB)
