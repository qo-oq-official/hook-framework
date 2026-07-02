"""PDB integration test: generate hooks.json, inject, verify raw output."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLBOX = Path(r"C:\Users\zwsoft\toolbox")
SCRIPT_DIR = Path(__file__).resolve().parent
BUILD_DIR = (SCRIPT_DIR / ".." / "build" / "bin" / "Release").resolve()

HOOK_DLL   = BUILD_DIR / "hook_framework.dll"
INJECTOR   = BUILD_DIR / "injector.exe"
DLL_TARGET = BUILD_DIR / "test_pdb_target.dll"
DLL_PDB    = BUILD_DIR / "test_pdb_target.pdb"
RUNNER     = BUILD_DIR / "test_pdb_runner.exe"
GEN_SCRIPT = SCRIPT_DIR / "generate_hooks_from_pdb.py"

ok = 0
bad = 0

def check(cond, msg):
    global ok, bad
    if cond:
        print(f"  [PASS] {msg}")
        ok += 1
    else:
        print(f"  [FAIL] {msg}")
        bad += 1

# Validate prerequisites
for f in [HOOK_DLL, INJECTOR, DLL_TARGET, DLL_PDB, RUNNER, GEN_SCRIPT]:
    if not f.exists():
        print(f"[ERROR] Missing: {f}")
        sys.exit(1)

test_dir = BUILD_DIR / "pdb_test"
shutil.rmtree(str(test_dir), ignore_errors=True)
(test_dir / "dump_data").mkdir(parents=True)

print("=== PDB Integration Test ===")

# Step 1: Generate hooks.json
print("[1] Generating hooks.json from PDB...")
hooks_json = test_dir / "hooks.json"
result = subprocess.run(
    [sys.executable, str(GEN_SCRIPT),
     "--contain", "pdb_",
     "--module", "test_pdb_target.dll",
     str(DLL_PDB), str(hooks_json)],
    capture_output=True, text=True, timeout=60,
)
if not hooks_json.exists():
    print(f"[ERROR] hooks.json not generated at {hooks_json}")
    print(f"  stderr: {result.stderr.strip()}")
    sys.exit(1)
print("     OK")

# Step 2: Copy binaries
print("[2] Copying binaries...")
for src in [HOOK_DLL, DLL_TARGET, RUNNER]:
    shutil.copy2(str(src), str(test_dir))
print("     OK")

# Step 3: Launch + inject via --exe mode (one step)
print("[3] Launching + injecting (--exe mode)...")
inj_result = subprocess.run(
    [
        str(INJECTOR), "--exe", str(test_dir / "test_pdb_runner.exe"),
        "--dll", str(test_dir / "hook_framework.dll"),
        "--args", "",
    ],
    cwd=str(test_dir),
    capture_output=True,
    text=True,
    timeout=60,
)
print(f"     Exit code: {inj_result.returncode}")
if inj_result.returncode != 0:
    print(f"     stdout: {inj_result.stdout.strip()}")
    print(f"     stderr: {inj_result.stderr.strip()}")

# Step 4: Verify
print("[4] Verifying Perfetto trace output...")
trace_files = list((test_dir / "dump_data").glob("trace_*.json"))
trace_count = len(trace_files)
print(f"     Trace files: {trace_count}")

check(trace_count >= 1, f"at least 1 trace file (got {trace_count})")

# Read trace and check for our function events
trace_events = []
if trace_files:
    try:
        data = json.loads(trace_files[0].read_text(encoding="utf-8"))
        trace_events = data.get("traceEvents", [])
    except Exception as e:
        print(f"     [WARN] Failed to parse trace: {e}")

event_names = set(e.get("name", "") for e in trace_events)
print(f"     Event count: {len(trace_events)}")
print(f"     Unique names: {len(event_names)}")

check(len(trace_events) > 0, "trace contains events")

expected = [
    "pdb_add", "pdb_sub", "pdb_mul_double", "pdb_div_float", "pdb_mul_i64",
    "pdb_sum4", "pdb_echo_str", "pdb_concat_w", "pdb_read_int", "pdb_read_double",
    "pdb_read_pair", "pdb_format", "pdb_set_flag", "pdb_is_positive", "pdb_deep_call",
    "pdb_deep_l3",
]
for name in expected:
    found = any(name in n for n in event_names)
    check(found, f"{name}: B/E events in trace")

# Check for Begin/End phase events
b_count = sum(1 for e in trace_events if e.get("ph") == "B")
e_count = sum(1 for e in trace_events if e.get("ph") == "E")
print(f"     Begin: {b_count}, End: {e_count}")
check(b_count > 0, "contains Begin events (ph:B)")
check(e_count > 0, "contains End events (ph:E)")

print(f"\n=== {ok} passed, {bad} failed ===")

# Cleanup
shutil.rmtree(str(test_dir), ignore_errors=True)
sys.exit(min(bad, 1))
