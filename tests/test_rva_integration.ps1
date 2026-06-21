$BinDir = "C:\Users\zwsoft\hook-framework\build\bin\Release"
$WorkDir = Join-Path $BinDir "test_rva_int"
New-Item $WorkDir -ItemType Directory -Force | Out-Null
Copy-Item "$BinDir\hook_framework.dll","$BinDir\test_target.dll","$BinDir\target_app.exe","$BinDir\injector.exe" $WorkDir -Force

$config = @{
    output_dir = "dump_data"; output_format = "perfetto"; log_level = "info"
    hooks = @(
        @{module="target_app.exe"; symbol="add_numbers"; action="capture"; params=@(@{name="a";type="int32"},@{name="b";type="int32"}); return_type="int32"}
        @{module="target_app.exe"; symbol="multiply_numbers"; action="capture"; params=@(@{name="a";type="int32"},@{name="b";type="int32"}); return_type="int32"}
        # RVA hook with display_name - non-exported internal function
        @{module="target_app.exe"; rva="0x2100"; display_name="internal_calculate"; action="capture"; params=@(@{name="x";type="int32"},@{name="y";type="int32"},@{name="op";type="int32"}); return_type="int32"}
    )
}
$config | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $WorkDir "hooks.json")

$injector = Join-Path $WorkDir "injector.exe"
$target = Join-Path $WorkDir "target_app.exe"
$dll = Join-Path $WorkDir "hook_framework.dll"

# Write stdin with Enter keys for target_app
$stdinFile = Join-Path $WorkDir "input.txt"
"`r`n`r`n" | Set-Content $stdinFile -NoNewline

# Run injector piping stdin
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $injector
$psi.Arguments = "--exe `"$target`" --dll `"$dll`""
$psi.WorkingDirectory = $WorkDir
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($psi)
$p.StandardInput.Write("`r`n`r`n")
$p.StandardInput.Close()
$p.WaitForExit(15000) | Out-Null

Start-Sleep 2

$traceDir = Join-Path $WorkDir "dump_data"
$trace = Get-ChildItem $traceDir -Filter "trace_*.json" | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $trace) { $trace = Join-Path $traceDir "trace.json" }
$ok = 0; $bad = 0
function Check($c, $n) { if ($c) { Write-Host "  [PASS] $n"; $global:ok++ } else { Write-Host "  [FAIL] $n"; $global:bad++ } }

Write-Host "`nVerifying..."
Check (Test-Path $trace) "trace.json exists"
if (Test-Path $trace) {
    $c = Get-Content $trace -Raw
    Check ($c -match "@RVA_") "contains @RVA_ suffix"
    $cnt = ([regex]::Matches($c, "@RVA_")).Count
    Check ($cnt -ge 6) "at least 6 @RVA_ occurrences, got $cnt"
    # Verify display_name for RVA hook
    Check ($c -match "internal_calculate @RVA_") "RVA hook uses display_name 'internal_calculate' not 'RVA_0x2100'"
    Check ($c -notmatch "`"name`":`"RVA_0x2100`"") "no raw RVA_0x2100 name when display_name is set"
    # Print sample names
    $j = $c | ConvertFrom-Json
    foreach ($ev in $j.traceEvents | Select-Object -First 6) { Write-Host "  $($ev.ph) $($ev.name)" }
}

Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "`n=== $ok passed, $bad failed ==="
exit $bad