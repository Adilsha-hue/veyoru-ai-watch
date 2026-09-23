param(
  [string]$Port = "COM15"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$PlatformIO = Join-Path $ProjectRoot ".venv-platformio\Scripts\platformio.exe"
$env:PLATFORMIO_CORE_DIR = Join-Path $ProjectRoot ".platformio-core"

if (-not (Test-Path -LiteralPath $PlatformIO)) {
  throw "PlatformIO is not installed at $PlatformIO"
}

Write-Host "Building VEYORU firmware..."
& $PlatformIO run --project-dir $PSScriptRoot
if ($LASTEXITCODE -ne 0) {
  throw "Build failed. The board was not changed."
}

Write-Host "Uploading VEYORU firmware to $Port..."
& $PlatformIO run --project-dir $PSScriptRoot --target upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
  throw "Upload failed. Check the USB cable, port, and BOOT/RESET sequence."
}

Write-Host "VEYORU firmware uploaded successfully."
