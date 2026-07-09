"""Extract restore(su_CArchive) functions from decompiled .dll.c files and merge into hooks.json."""
import json
import os
import re
import sys
from pathlib import Path
from collections import OrderedDict

DECOMPILE_DIR = r"C:\Users\zwsoft\ws\sld_reversing\solidworks_2024"
HOOKS_PATH = Path(__file__).resolve().parent / "hooks.json"

# ImageBase per DLL (determined from first .text function VA = base + 0x1000)
DLL_BASES = {
    "sldmodu.dll":     0x4AA40000,
    "sldmfcu.dll":     0x39440000,
    "sldMoPartu.dll":  0x180000000,
    "sldarchiveu.dll": 0x2EB50000,
    "sldappu.dll":     0x42D00000,
    "swccu.dll":       0x2EB90000,  # keep for completeness
}

RESTORE_PATTERN = re.compile(r"restore.*su_CArchive|su_CArchive.*restore", re.IGNORECASE)
VA_PATTERN = re.compile(r"^//----- \(0([0-9A-Fa-f]+)\)")


def extract_restore_functions(dll_c_path: str) -> list:
    dll_name = os.path.basename(dll_c_path).replace(".dll.c", ".dll")
    base = DLL_BASES.get(dll_name, 0)
    if base == 0:
        return []

    hooks = []
    current_va = 0
    with open(dll_c_path, errors="ignore") as f:
        for line in f:
            m = VA_PATTERN.match(line)
            if m:
                current_va = int(m.group(1), 16)
                continue
            if current_va == 0:
                continue
            sig = line.strip()
            if sig and not sig.startswith("//") and RESTORE_PATTERN.search(sig):
                # Must be a function definition: starts with return type (not call site)
                if not re.match(r"^(void|int|__int64|char|bool|double|float|unsigned|signed|long|short|wchar_t|size_t|HRESULT)\b", sig):
                    current_va = 0
                    continue
                rva = current_va - base
                hooks.append({
                    "module": dll_name,
                    "rva": f"0x{rva:x}",
                    "action": "capture",
                    "mode": "capture",
                    "collector": "typed",
                    "display_name": re.sub(r"\s+", " ", sig),
                    "params": [
                        {"name": "this", "type": "pointer"},
                        {"name": "archive", "type": "su_CArchivePtr", "capture_on_exit": True},
                    ],
                })
                current_va = 0
    return hooks


def main():
    hooks_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HOOKS_PATH

    # Load existing hooks
    print(f"Loading: {hooks_path}")
    existing = json.loads(hooks_path.read_text(encoding="utf-8"))
    existing_hooks = existing.get("hooks", [])
    print(f"  Existing: {len(existing_hooks)} hooks")

    # Build seen index: (module, rva) → existing entry
    seen = OrderedDict()
    for h in existing_hooks:
        key = (h["module"], h.get("rva", h.get("symbol", "")))
        seen[key] = h

    # Extract restore functions from all .dll.c files
    new_count = 0
    for fname in sorted(os.listdir(DECOMPILE_DIR)):
        if not fname.endswith(".dll.c"):
            continue
        dll_path = os.path.join(DECOMPILE_DIR, fname)
        dll_name = fname.replace(".dll.c", ".dll")
        hooks = extract_restore_functions(dll_path)
        if not hooks:
            continue

        added = 0
        for h in hooks:
            key = (h["module"], h["rva"])
            if key not in seen:
                seen[key] = h
                added += 1
                new_count += 1
        print(f"  {dll_name}: {len(hooks)} found, {added} new")

    # Rebuild hooks array (preserve original order + append new)
    merged = list(seen.values())
    existing["hooks"] = merged

    hooks_path.write_text(json.dumps(existing, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nTotal: {len(merged)} hooks ({new_count} new)")

    # Validate RVA range per DLL
    dlls = set(h["module"] for h in merged)
    for dll in sorted(dlls):
        rvas = [int(h["rva"], 16) for h in merged if h["module"] == dll and "rva" in h]
        syms = [h for h in merged if h["module"] == dll and "symbol" in h]
        if rvas:
            print(f"  {dll}: {len(rvas)} RVA hooks, RVA 0x{min(rvas):x}-0x{max(rvas):x}")
        if syms:
            print(f"  {dll}: {len(syms)} symbol hooks")


if __name__ == "__main__":
    main()
