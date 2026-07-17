"""Add vftable serialize hooks from CSV files into hooks.json.

Rule: if vftable[2] == '?Serialize@su_CObject@@UEAAXAEAVCArchive@@@Z'
       use vftable[5], otherwise use vftable[2].
Display name = entity column.
sub_XXXXXXXX entries: RVA = VA - ImageBase.
MSVC symbol names: look up RVA from existing hooks.json.
"""
import csv
import json
import re
import sys
from pathlib import Path
from collections import OrderedDict

SCRIPT_DIR = Path(__file__).resolve().parent
HOOKS_PATH = SCRIPT_DIR / "hooks.json"
CSV_DIR = SCRIPT_DIR

BASES = {
    "swccu.dll":    0x2EB90000,
    "sldmfcu.dll":  0x39440000,
    "sldmodu.dll":  0x4AA40000,
}


def load_symbol_map(hooks: list) -> dict:
    """Build (module, symbol) → rva map from existing hooks."""
    symbol_map = {}
    for h in hooks:
        sym = h.get("symbol", "")
        if sym:
            key = (h["module"], sym)
            symbol_map[key] = h.get("rva", "")
    return symbol_map


def resolve_rva(func_name: str, dll: str, symbol_map: dict) -> str:
    """Resolve a function name to an RVA string like '0xnnnn'."""
    # sub_XXXXXXXX → extract VA directly
    m = re.match(r"^sub_([0-9A-Fa-f]+)$", func_name)
    if m:
        base = BASES.get(dll, 0)
        va = int(m.group(1), 16)
        return f"0x{va - base:x}"

    # MSVC symbol → look up in symbol_map
    key = (dll, func_name)
    return symbol_map.get(key, "")


def process_csv(csv_path: Path, symbol_map: dict) -> list:
    hooks = []
    with open(csv_path, encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            dll = row.get("dll", "").strip()
            entity = row.get("entity", "").strip()
            v2 = row.get("vftable[2]", "").strip()
            v5 = row.get("vftable[5]", "").strip()

            if not dll or not entity:
                continue

            # Apply selection rule
            target = v5 if v2 == "?Serialize@su_CObject@@UEAAXAEAVCArchive@@@Z" else v2
            if not target:
                continue

            rva = resolve_rva(target, dll, symbol_map)
            if not rva:
                continue  # skip unresolvable

            hooks.append({
                "module": dll,
                "rva": rva,
                "action": "capture",
                "mode": "capture",
                "collector": "typed",
                "display_name": entity,
            })
    return hooks


def main():
    # Load existing hooks + build symbol→RVA map
    print(f"Loading: {HOOKS_PATH}")
    existing = json.loads(HOOKS_PATH.read_text(encoding="utf-8"))
    existing_hooks = existing.get("hooks", [])
    print(f"  Existing: {len(existing_hooks)} hooks")
    symbol_map = load_symbol_map(existing_hooks)

    # Build seen index
    seen = OrderedDict()
    for h in existing_hooks:
        key = (h["module"], h.get("rva", h.get("symbol", "")))
        seen[key] = h

    # Process CSV files
    total_new = 0
    for csv_name in sorted(CSV_DIR.glob("*_serialize.csv")):
        dll_name = csv_name.stem.replace("_serialize", "") + ".dll"
        print(f"  Processing {csv_name.name} ({dll_name})...")
        hooks = process_csv(csv_name, symbol_map)
        added = 0
        for h in hooks:
            key = (h["module"], h["rva"])
            if key not in seen:
                seen[key] = h
                added += 1
                total_new += 1
        resolvable = len(hooks)
        print(f"  {dll_name}: {resolvable} found, {added} new")

    # Save merged
    merged = list(seen.values())
    existing["hooks"] = merged
    HOOKS_PATH.write_text(json.dumps(existing, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nTotal: {len(merged)} hooks ({total_new} new)")

    # Summary per DLL
    for dll in sorted(set(h["module"] for h in merged)):
        cnt = sum(1 for h in merged if h["module"] == dll)
        print(f"  {dll}: {cnt} hooks")


if __name__ == "__main__":
    main()
