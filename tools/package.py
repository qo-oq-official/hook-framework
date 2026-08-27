"""Package hook_framework + PDB tools into standalone deployment directory.

Output: package/hook-framework/
  bin/      hook_framework.dll, injector.exe
  tools/    DIA2Dump.exe, msdia140.dll, Python scripts
  hooks/    example hooks.json template
"""
import shutil
import sys
from pathlib import Path
import subprocess
from datetime import datetime


def get_git_hash():
  try:
    git_hash = (
        subprocess.check_output(['git', 'rev-parse', 'HEAD'])
        .strip()
        .decode('utf-8')
    )
    return git_hash
  except Exception:
    return 'Unknown'

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
BUILD_DIR = ROOT / "build" / "bin" / "Release"
TOOLBOX = Path(r"C:\Users\zwsoft\toolbox")
PACK_NAME = f"hook-framework-{datetime.now().strftime("%Y_%m_%d_%H_%M_%S")}-{get_git_hash()[:8]}"
PKG_ROOT = ROOT / "package" / PACK_NAME

# Clean old package
shutil.rmtree(str(PKG_ROOT), ignore_errors=True)

# ── Directories ─────────────────────────────────────────────────────────

for d in ["bin", "tools", "configuration", "perfetto"]:
    (PKG_ROOT / d).mkdir(parents=True)

# ── Binaries ────────────────────────────────────────────────────────────

print("[1] Copying binaries...")
for name in ["hook_framework.dll", "injector.exe", "etw_loader.exe"]:
    src = BUILD_DIR / name
    if src.exists():
        shutil.copy2(str(src), str(PKG_ROOT / "bin" / name))
        print(f"     bin/{name}")
    else:
        print(f"  [WARN] Missing: {src}")

# ── PDB tools ───────────────────────────────────────────────────────────

print("[2] Copying PDB tools...")
for name in ["DIA2Dump.exe", "msdia140.dll"]:
    src = TOOLBOX / name
    if src.exists():
        shutil.copy2(str(src), str(PKG_ROOT / "tools" / name))
        print(f"     tools/{name}")
    else:
        print(f"  [WARN] Missing: {src}")

# ── PDB tools ───────────────────────────────────────────────────────────

print("[2] Copying Perfetto tools...")
for name in ["perfetto.exe", "traced.exe", "trace_processor_shell.exe", "traceconv.exe", "ui.zip"]:
    src = TOOLBOX / "perfetto" / "windows-amd64" / name
    if src.exists():
        shutil.copy2(str(src), str(PKG_ROOT / "perfetto" / name))
        print(f"     perfetto/{name}")
    else:
        print(f"  [WARN] Missing: {src}")

    

# ── Python scripts ──────────────────────────────────────────────────────

print("[3] Copying Python scripts...")
scripts = [
    "generate_hooks_from_pdb.py",
    "symbolize_trace.py",
    "dump_pdb_path.py"
]
for name in scripts:
    src = SCRIPT_DIR / name
    if src.exists():
        shutil.copy2(str(src), str(PKG_ROOT / "tools" / name))
        print(f"     tools/{name}")
    else:
        print(f"  [WARN] Missing: {src}")

# ── Example configuration ──────────────────────────────────────────────────

print("[4] Generating example configuration...")
example = """
{
  "output_dir": "dump_data",
  "output_format": "perfetto",
  "hooks": [
    {
      "module": "target.dll",
      "rva": "0x1100",
      "action": "capture",
      "mode": "capture",
      "collector": "raw",
      "display_name": "example_function"
    }
  ]
}
"""
(PKG_ROOT / "configuration" / "example_hooks.json").write_text(example.strip() + "\n")
print("     configuration/example_hooks.json")

example = """
{
  "session_name": "HookFramework-DllLoader",
  "rules": [
    {
      "on_load": "test_pdb_target.dll",
      "inject": "path/to/hook_framework.dll"
    }
  ]
}
"""
(PKG_ROOT / "configuration" / "example_etw_load_config.json").write_text(example.strip() + "\n")
print("     configuration/example_etw_load_config.json")

example = """
{
    "pdb_config": [
        {
            "pdb_path": "",
            "function_white_list": [
            ],
            "function_black_list": [

            ]
        }
    ]
}
"""
(PKG_ROOT / "configuration" / "example_pdb_config.json").write_text(example.strip() + "\n")
print("     configuration/example_pdb_config.json")

# ── README ──────────────────────────────────────────────────────────────

print("[5] Writing README...")
readme = """# Hook Framework Deployment Package

## Directory Structure

```
bin/        hook_framework.dll, injector.exe
tools/      DIA2Dump.exe, msdia140.dll, Python scripts
configuration/      example_hooks.json example_etw_load_config.json configuration files
```

## Usage

### 1. Generate hooks.json from PDB

powershell
python tools\\generate_hooks_from_pdb.py --config pdb_config.json hooks\\hooks.json"


### 2. Hook a running process

powershell
# Start target process with injection window
target.exe --wait 3 &

# Inject framework
bin\\injector.exe --pid <PID> --dll bin\\hook_framework.dll


### 3. Validate hooks.json

powershell
python tools\\validate_hooks.py


### 4. Symbolize captured Perfetto trace (RVA -> function + file:line)

powershell
python tools\\symbolize_trace.py --trace record.perfetto-trace \\
    --pdb-config configuration\\pdb_config.json \\
    --output report.json --augmented record.symbolized.perfetto-trace


## Notes

- hooks.json must be named "hooks.json" and placed in the same directory as hook_framework.dll
- DIA2Dump.exe requires msdia140.dll in the same directory
- For Perfetto trace output, set "output_format": "perfetto" in hooks.json
"""
(PKG_ROOT / "README.md").write_text(readme.strip() + "\n")
print("     README.md")

print("[6] Archive")
shutil.make_archive(PKG_ROOT, "zip", PKG_ROOT)

print("[7] Dispatch")
print(f"copy {PKG_ROOT.with_suffix(".zip")} '\\\\pubshares.zwcax.com\\FTPServer\\FileTransfer\\Gesic\\chensonghui\\downloads'")
shutil.copy(PKG_ROOT.with_suffix(".zip"), '\\\\pubshares.zwcax.com\\FTPServer\\FileTransfer\\Gesic\\chensonghui\\downloads')
# ── Summary ─────────────────────────────────────────────────────────────

print(f"\nPackage created: {PKG_ROOT}")
files = sorted(str(p.relative_to(PKG_ROOT)) for p in PKG_ROOT.rglob("*") if p.is_file())
for f in files:
    print(f"  {f}")
print(f"\nTotal: {len(files)} files")
