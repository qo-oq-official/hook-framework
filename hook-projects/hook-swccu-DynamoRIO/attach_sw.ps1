# DynamoRIO attach script for SOLIDWORKS
# Attaches hook_framework.dll to running SLDWORKS.exe
param(
    [string]$ConfigFile = "hooks_dev.json"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$DRRoot = "C:\Users\zwsoft\toolbox\DynamoRIO\DynamoRIO-Windows-11.91.20602"
$DRRun = Join-Path $DRRoot "bin64\drrun.exe"
$BuildDir = "C:\Users\zwsoft\hook-framework-DynamoRIO\build\bin\Release"
$HookDll = Join-Path $BuildDir "hook_framework.dll"

Write-Host "=== DynamoRIO SOLIDWORKS Hook ===" -ForegroundColor Cyan
Write-Host ""

# Copy DLL to working directory
$LocalDll = Join-Path $ScriptDir "hook_framework.dll"
Copy-Item $HookDll $LocalDll -Force
Write-Host "[*] hook_framework.dll copied: $LocalDll"

$ConfigPath = Join-Path $ScriptDir $ConfigFile
if (-not (Test-Path $ConfigPath)) {
    Write-Error "Config not found: $ConfigPath"
    exit 1
}
Write-Host "[*] Config: $ConfigFile"

# Ensure output directory
$OutDir = Join-Path $ScriptDir "dump_data"
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory $OutDir -Force | Out-Null }

# Find SLDWORKS.exe
$sw = Get-Process -Name "SLDWORKS" -ErrorAction SilentlyContinue
if (-not $sw) {
    Write-Host "[!] SLDWORKS.exe not running." -ForegroundColor Red
    Write-Host "    Start SOLIDWORKS first."
    exit 1
}
$Pid = $sw[0].Id
Write-Host "[*] SLDWORKS.exe PID=$Pid"

# Verify target DLLs
$targetDlls = @("swccu.dll", "sldmodu.dll", "sldappu.dll", "sldmfcu.dll")
foreach ($dll in $targetDlls) {
    $loaded = $sw[0].Modules | Where-Object { $_.ModuleName -eq $dll }
    if ($loaded) { Write-Host "[*] $dll loaded" }
    else { Write-Host "[!] $dll NOT loaded yet" -ForegroundColor Yellow }
}

# Attach
Write-Host ""
Write-Host "[*] Attaching DR to PID=$Pid ..." -ForegroundColor Green
$args = @("-attach", $Pid, "-c", $LocalDll, "--", "--config", $ConfigPath)
Write-Host "    drrun.exe $($args -join ' ')"
& $DRRun @args

Write-Host ""
Write-Host "[*] Done. Output in: $OutDir" -ForegroundColor Green
Write-Host "[*] Diagnostic: hook_framework_diag.txt"
