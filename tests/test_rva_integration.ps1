$BinDir = "C:\Users\zwsoft\hook-framework\build\bin\Release"
$WorkDir = Join-Path $BinDir "test_rva_int"
New-Item $WorkDir -ItemType Directory -Force | Out-Null
Copy-Item "$BinDir\hook_framework.dll","$BinDir\test_target.dll","$BinDir\target_app.exe","$BinDir\injector.exe" $WorkDir -Force

$config = @{
    output_dir = "dump_data"; output_format = "perfetto"; log_level = "info"
    hooks = @(
        @{module="target_app.exe"; symbol="add_numbers"; action="capture"; params=@(@{name="a";type="int32"},@{name="b";type="int32"}); return_type="int32"}
        @{module="target_app.exe"; symbol="multiply_numbers"; action="capture"; params=@(@{name="a";type="int32"},@{name="b";type="int32"}); return_type="int32"}
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

$trace = Join-Path $WorkDir "dump_data\trace.json"
$ok = 0; $bad = 0
function Check($c, $n) { if ($c) { Write-Host "  [PASS] $n"; $global:ok++ } else { Write-Host "  [FAIL] $n"; $global:bad++ } }

Write-Host "`nVerifying..."
Check (Test-Path $trace) "trace.json exists"
if (Test-Path $trace) {
    $c = Get-Content $trace -Raw
    Check ($c -match "@RVA_") "contains @RVA_ suffix"
    $cnt = ([regex]::Matches($c, "@RVA_")).Count
    Check ($cnt -ge 4) "at least 4 @RVA_ occurrences, got $cnt"
    # Print sample names
    $j = $c | ConvertFrom-Json
    foreach ($ev in $j.traceEvents | Select-Object -First 4) { Write-Host "  $($ev.ph) $($ev.name)" }
}

Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "`n=== $ok passed, $bad failed ==="
exit $bad