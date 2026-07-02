# Hook Framework Deployment Package

## Directory Structure

```
bin/        hook_framework.dll, injector.exe
tools/      DIA2Dump.exe, msdia140.dll, Python scripts
hooks/      hooks.json configuration files
```

## Usage

### 1. Generate hooks.json from PDB

powershell
python tools\generate_hooks_from_pdb.py target.pdb hooks\hooks.json --prefix "my_prefix_"


### 2. Hook a running process

powershell
# Start target process with injection window
target.exe --wait 3 &

# Inject framework
bin\injector.exe --pid <PID> --dll bin\hook_framework.dll


### 3. Validate hooks.json

powershell
python tools\validate_hooks.py


## Notes

- hooks.json must be named "hooks.json" and placed in the same directory as hook_framework.dll
- DIA2Dump.exe requires msdia140.dll in the same directory
- For Perfetto trace output, set "output_format": "perfetto" in hooks.json
