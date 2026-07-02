"""Package hook_framework + PDB tools into standalone deployment directory.

Output: package/hook-framework/
  bin/      hook_framework.dll, injector.exe
  tools/    DIA2Dump.exe, msdia140.dll, Python scripts
  hooks/    example hooks.json template
"""
import shutil
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
BUILD_DIR = ROOT / "build" / "bin" / "Release"
TOOLBOX = Path(r"C:\Users\zwsoft\toolbox")
PKG_ROOT = ROOT / "package" / "hook-framework"

# Clean old package
shutil.rmtree(str(PKG_ROOT), ignore_errors=True)

# ── Directories ─────────────────────────────────────────────────────────

for d in ["bin", "tools", "hooks"]:
    (PKG_ROOT / d).mkdir(parents=True)

# ── Binaries ────────────────────────────────────────────────────────────

print("[1] Copying binaries...")
for name in ["hook_framework.dll", "injector.exe"]:
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

# ── Python scripts ──────────────────────────────────────────────────────

print("[3] Copying Python scripts...")
scripts = [
    "generate_hooks_from_pdb.py",
    "validate_hooks.py",
]
for name in scripts:
    src = SCRIPT_DIR / name
    if src.exists():
        shutil.copy2(str(src), str(PKG_ROOT / "tools" / name))
        print(f"     tools/{name}")
    else:
        print(f"  [WARN] Missing: {src}")

# ── Example hooks.json ──────────────────────────────────────────────────

print("[4] Generating example hooks.json...")
example = """
{
  "output_dir": "dump_data",
  "output_format": "file",
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
(PKG_ROOT / "hooks" / "example_hooks.json").write_text(example.strip() + "\n")
print("     hooks/example_hooks.json")

# ── README ──────────────────────────────────────────────────────────────

print("[5] Writing README...")
readme = """# Hook Framework Deployment Package

## Directory Structure

```
bin/        hook_framework.dll, injector.exe
tools/      DIA2Dump.exe, msdia140.dll, Python scripts
hooks/      hooks.json configuration files
```

## Usage

### 1. Generate hooks.json from PDB

powershell
python tools\\generate_hooks_from_pdb.py target.pdb hooks\\hooks.json --prefix "my_prefix_"


### 2. Hook a running process

powershell
# Start target process with injection window
target.exe --wait 3 &

# Inject framework
bin\\injector.exe --pid <PID> --dll bin\\hook_framework.dll


### 3. Validate hooks.json

powershell
python tools\\validate_hooks.py


## Notes

- hooks.json must be named "hooks.json" and placed in the same directory as hook_framework.dll
- DIA2Dump.exe requires msdia140.dll in the same directory
- For Perfetto trace output, set "output_format": "perfetto" in hooks.json
"""
(PKG_ROOT / "README.md").write_text(readme.strip() + "\n")
print("     README.md")

# ── Summary ─────────────────────────────────────────────────────────────

print(f"\nPackage created: {PKG_ROOT}")
files = sorted(str(p.relative_to(PKG_ROOT)) for p in PKG_ROOT.rglob("*") if p.is_file())
for f in files:
    print(f"  {f}")
print(f"\nTotal: {len(files)} files")
