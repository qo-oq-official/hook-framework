"""Validate hooks.json against actual SOLIDWORKS DLL PE files.

Checks:
  - symbol hook: symbol exists in DLL export table
  - rva hook:    RVA falls within executable (.text) section
  - module:      DLL file exists

Usage:
  python tools/validate_hooks.py [--output report.json]
"""
import argparse
import json
import sys
import os
from pathlib import Path
from collections import defaultdict

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from pe_parser import PEFile

# ── config ────────────────────────────────────────────────────────────────

SW_DLL_DIR = r"C:\Program Files\SOLIDWORKS Corp 2024\SOLIDWORKS"
HOOKS_PATH = (SCRIPT_DIR / ".." / "hook-projects" / "hook-swccu" / "hooks.json").resolve()

# ── helpers ────────────────────────────────────────────────────────────────

def load_pe_cache(dll_dir):
    """Build a mapping of module name → PEFile (lazy-loaded on first use)."""
    cache = {}
    for f in Path(dll_dir).glob("*.dll"):
        cache[f.name.lower()] = str(f)
    return cache


def validate_hooks(hooks):
    dll_paths = load_pe_cache(SW_DLL_DIR)
    pe_cache = {}  # module → PEFile (only loaded when needed)
    results = defaultdict(list)  # module → [errors]
    valid_count = 0
    invalid_count = 0
    by_module = defaultdict(lambda: {"valid": 0, "invalid": 0})

    def get_pe(module):
        key = module.lower()
        if key in pe_cache:
            return pe_cache[key]
        if key not in dll_paths:
            return None
        try:
            pe = PEFile(dll_paths[key])
            pe_cache[key] = pe
            return pe
        except Exception as e:
            results[module].append(f"pe_load_error: {e}")
            return None

    for i, hook in enumerate(hooks):
        module = hook.get("module", "?")
        symbol = hook.get("symbol")
        rva_str = hook.get("rva")
        display = hook.get("display_name", f"hook[{i}]")

        pe = get_pe(module)
        if pe is None:
            results[module].append(f"{display}: dll_not_found")
            invalid_count += 1
            by_module[module]["invalid"] += 1
            continue

        if symbol:
            if pe.has_export(symbol):
                valid_count += 1
                by_module[module]["valid"] += 1
            else:
                results[module].append(f"{display}: symbol_not_found ({symbol})")
                invalid_count += 1
                by_module[module]["invalid"] += 1
        elif rva_str:
            try:
                rva = int(rva_str, 16)
            except ValueError:
                results[module].append(f"{display}: bad_rva_format ({rva_str})")
                invalid_count += 1
                by_module[module]["invalid"] += 1
                continue
            if rva == 0:
                results[module].append(f"{display}: rva_is_zero")
                invalid_count += 1
                by_module[module]["invalid"] += 1
            elif pe.is_code_rva(rva):
                valid_count += 1
                by_module[module]["valid"] += 1
            else:
                results[module].append(f"{display}: rva_0x{rva:x}_not_in_code_section")
                invalid_count += 1
                by_module[module]["invalid"] += 1
        else:
            results[module].append(f"{display}: no_symbol_or_rva")
            invalid_count += 1
            by_module[module]["invalid"] += 1

    return {
        "summary": {
            "total": len(hooks),
            "valid": valid_count,
            "invalid": invalid_count,
        },
        "by_module": dict(by_module),
        "errors": dict(results),
    }


def print_report(report):
    s = report["summary"]
    print(f"\n=== Hook Validation Report ===")
    print(f"DLL dir : {SW_DLL_DIR}")
    print(f"Hooks   : {s['total']} total")
    print(f"  valid : {s['valid']}")
    print(f"  invalid: {s['invalid']}")
    print(f"  rate  : {s['invalid'] / s['total'] * 100:.2f}% error" if s['total'] > 0 else "")

    for module, counts in sorted(report["by_module"].items()):
        tag = ""
        if counts["invalid"] > 0:
            tag = f"  ** {counts['invalid']} INVALID **"
        print(f"\n  {module}: {counts['valid']} ok, {counts['invalid']} fail{tag}")

    errors = report.get("errors", {})
    if errors:
        print(f"\n=== Errors ({sum(len(v) for v in errors.values())}) ===")
        for module, errs in sorted(errors.items()):
            for e in errs[:10]:
                print(f"  [{module}] {e}")
            if len(errs) > 10:
                print(f"  ... and {len(errs) - 10} more for {module}")


def main():
    ap = argparse.ArgumentParser(
        description="Validate hooks.json against actual DLL PE files.",
    )
    ap.add_argument("--output", help="Output report JSON path")
    args = ap.parse_args()

    output_file = args.output

    if not HOOKS_PATH.exists():
        print(f"ERROR: hooks.json not found: {HOOKS_PATH}")
        sys.exit(1)
    if not Path(SW_DLL_DIR).is_dir():
        print(f"ERROR: SW DLL dir not found: {SW_DLL_DIR}")
        sys.exit(1)

    print(f"Loading hooks from: {HOOKS_PATH}")
    with open(HOOKS_PATH, 'r', encoding='utf-8') as f:
        config = json.load(f)

    hooks = config.get("hooks", [])
    print(f"Loaded {len(hooks)} hooks, validating...")

    report = validate_hooks(hooks)
    print_report(report)

    if output_file:
        out_path = (SCRIPT_DIR / ".." / output_file).resolve()
        out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        print(f"\nReport saved to: {out_path}")

    if report["summary"]["invalid"] > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
