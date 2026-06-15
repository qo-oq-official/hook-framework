#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Hook swccu.dll!su_CArchive::ReadClass in SLDWORKS.exe
.DESCRIPTION
    Injects hook_framework.dll into running SLDWORKS.exe process to capture
    su_CArchive::ReadClass(CRuntimeClass const *, uint *, ulong *) calls.

    Output goes to: dump_data/swccu.dll/RVA_0x2eb9a2e0/
.NOTES
    Must run as Administrator (SOLIDWORKS requires elevated injection)
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildBin = Join-Path $ScriptDir "..\..\build\bin\Release"
$WorkDir = $ScriptDir

$HookDll = Join-Path $BuildBin "hook_framework.dll"
$Injector = Join-Path $BuildBin "injector.exe"

if (-not (Test-Path $HookDll)) {
    Write-Error "hook_framework.dll not found at: $HookDll`nRun 'cmake --build build --config Release' first."
    exit 1
}
if (-not (Test-Path $Injector)) {
    Write-Error "injector.exe not found at: $Injector"
    exit 1
}

Write-Host "=== Hook swccu.dll!su_CArchive::ReadClass ===" -ForegroundColor Cyan
Write-Host ""

# Copy DLL to working directory (hooks.json must be next to DLL)
$LocalDll = Join-Path $WorkDir "hook_framework.dll"
Copy-Item $HookDll $LocalDll -Force
Write-Host "[*] hook_framework.dll copied to working directory"

# Find SLDWORKS.exe
$sw = Get-Process -Name "SLDWORKS" -ErrorAction SilentlyContinue
if (-not $sw) {
    Write-Host "[!] SLDWORKS.exe is not running." -ForegroundColor Yellow
    Write-Host "    Start SOLIDWORKS first, then re-run this script."
    exit 1
}

$procId = $sw[0].Id
Write-Host "[*] Found SLDWORKS.exe PID=$procId"

# Verify target DLLs are loaded
$targetDlls = @("swccu.dll", "sldappu.dll", "sldmodu.dll", "sldarchiveu.dll", "sldmfcu.dll")
foreach ($dll in $targetDlls) {
    $loaded = $sw[0].Modules | Where-Object { $_.ModuleName -eq $dll }
    if (-not $loaded) {
        Write-Host "[!] $dll not loaded in SLDWORKS.exe yet." -ForegroundColor Yellow
        Write-Host "    Open a document or wait for SOLIDWORKS to fully load."
        exit 1
    }
    Write-Host "[*] $dll loaded at base: 0x$($loaded.BaseAddress.ToString('X'))"
}

# Inject
Write-Host ""
Write-Host "[*] Injecting hook_framework.dll into PID=$procId ..." -ForegroundColor Green
& $Injector --pid $procId --dll $LocalDll

if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Injection failed with code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[*] Injection successful!" -ForegroundColor Green
Write-Host ""
Write-Host "Now use SOLIDWORKS normally. Each call to su_CArchive::ReadClass"
Write-Host "will be captured to:"
Write-Host "  $WorkDir\dump_data\swccu.dll\RVA_0x2eb9a2e0\" -ForegroundColor Yellow
Write-Host ""
Write-Host "Files:"
Write-Host "  NNNNNN_entry.json  - Function entry params (this, pRuntimeClass, pUint, pUlong)"
Write-Host "  NNNNNN_entry.raw   - Raw register/stack snapshot (768 bytes)"
Write-Host "  NNNNNN_exit.json   - Return value"
Write-Host "  NNNNNN_exit.raw    - Raw post-call snapshot"
Write-Host ""
Write-Host "Log file: $WorkDir\hook_swccu.log"
Write-Host ""
Write-Host "Press Ctrl+C when done. Close SOLIDWORKS to unload hooks cleanly."
