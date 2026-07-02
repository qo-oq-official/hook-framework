# PDB integration test — generates hooks.json via DIA2Dump, injects, verifies raw output
param([int]$WaitSec = 3)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$BuildDir = Join-Path $ScriptDir "..\build\bin\Release"
$Toolbox = "C:\Users\zwsoft\toolbox"

$HookDll    = Join-Path $BuildDir "hook_framework.dll"
$Injector   = Join-Path $BuildDir "injector.exe"
$DllTarget  = Join-Path $BuildDir "test_pdb_target.dll"
$DllPdb     = Join-Path $BuildDir "test_pdb_target.pdb"
$Runner     = Join-Path $BuildDir "test_pdb_runner.exe"
$Dia2Dump   = Join-Path $Toolbox "DIA2Dump.exe"
$GenScript  = Join-Path $ScriptDir "generate_hooks_from_pdb.py"

foreach ($f in @($HookDll, $Injector, $DllTarget, $DllPdb, $Runner, $Dia2Dump, $GenScript)) {
    if (-not (Test-Path $f)) { Write-Error "Missing: $f"; exit 1 }
}

$TestDir = Join-Path $BuildDir "pdb_test"
Remove-Item $TestDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$TestDir\dump_data" -Force | Out-Null

Write-Host "=== PDB Integration Test ==="

# Step 1: Generate hooks.json from PDB
Write-Host "[1] Generating hooks.json from PDB..."
$hooksJson = Join-Path $TestDir "hooks.json"
python $GenScript $DllPdb (Join-Path $TestDir "hooks.json") 2>&1 | Out-Null
if (-not (Test-Path $hooksJson)) { Write-Error "hooks.json not generated"; exit 1 }
Write-Host "     OK"

# Step 2: Copy files
Write-Host "[2] Copying binaries..."
Copy-Item $HookDll, $DllTarget, $Runner $TestDir -Force
Write-Host "     OK"

# Step 3: Start runner with injection window
Write-Host "[3] Starting test_pdb_runner (wait=${WaitSec}s)..."
$proc = Start-Process -FilePath (Join-Path $TestDir "test_pdb_runner.exe") `
                       -ArgumentList "--wait","$WaitSec" `
                       -WorkingDirectory $TestDir `
                       -PassThru `
                       -NoNewWindow
$pid = $proc.Id
Write-Host "     PID=$pid"

# Step 4: Inject
Start-Sleep -Milliseconds 500
Write-Host "[4] Injecting hook_framework.dll into PID=$pid..."
$result = & $Injector --pid $pid --dll (Join-Path $TestDir "hook_framework.dll") 2>&1
Write-Host "     Injector output: $result"
Write-Host "     Exit code: $LASTEXITCODE"

# Step 5: Wait for runner to finish
Write-Host "[5] Waiting for runner to complete..."
$proc.WaitForExit(30000) | Out-Null
Write-Host "     Runner exit code: $($proc.ExitCode)"

# Step 6: Verify raw output files
Write-Host "[6] Verifying raw output files..."
$rawFiles = Get-ChildItem "$TestDir\dump_data" -Recurse -Filter "*.raw" -ErrorAction SilentlyContinue
$rawCount = $rawFiles.Count
Write-Host "     Raw files found: $rawCount"

# Each function should have at least entry.raw + exit.raw
$funcDirs = Get-ChildItem "$TestDir\dump_data\test_pdb_target.dll" -Directory -ErrorAction SilentlyContinue
$funcCount = $funcDirs.Count
Write-Host "     Function dirs: $funcCount"

$ok = 0; $bad = 0
function Check($cond, $msg) { if ($cond) { Write-Host "  [PASS] $msg"; $script:ok++ } else { Write-Host "  [FAIL] $msg"; $script:bad++ } }

Check ($rawCount -ge 20) "at least 20 raw files (got $rawCount)"

# Check specific functions have entry+exit
$expected = @("pdb_add", "pdb_sub", "pdb_mul_double", "pdb_div_float", "pdb_mul_i64",
              "pdb_sum4", "pdb_echo_str", "pdb_concat_w", "pdb_read_int", "pdb_read_double",
              "pdb_read_pair", "pdb_format", "pdb_set_flag", "pdb_is_positive", "pdb_deep_call",
              "pdb_deep_l3", "pdb_alloc", "pdb_free")
foreach ($name in $expected) {
    $dir = Join-Path $TestDir "dump_data\test_pdb_target.dll\$name"
    $files = Get-ChildItem $dir -Filter "*.raw" -ErrorAction SilentlyContinue
    Check ($files.Count -ge 2) "$name : at least 2 raw files (got $($files.Count))"
}

# Check static functions too
$staticDir = Join-Path $TestDir "dump_data\test_pdb_target.dll\static int pdb_deep_l1(int)"
$staticFiles = Get-ChildItem $staticDir -Filter "*.raw" -ErrorAction SilentlyContinue
Check ($staticFiles.Count -ge 2) "static pdb_deep_l1 : at least 2 raw files (got $($staticFiles.Count))"

$staticDir2 = Join-Path $TestDir "dump_data\test_pdb_target.dll\static int pdb_deep_l2(int)"
$staticFiles2 = Get-ChildItem $staticDir2 -Filter "*.raw" -ErrorAction SilentlyContinue
Check ($staticFiles2.Count -ge 2) "static pdb_deep_l2 : at least 2 raw files (got $($staticFiles2.Count))"

Write-Host "`n=== $ok passed, $bad failed ==="

# Cleanup
Remove-Item $TestDir -Recurse -Force -ErrorAction SilentlyContinue
exit $bad
