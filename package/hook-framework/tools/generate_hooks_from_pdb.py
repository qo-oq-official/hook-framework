"""Generate hooks.json from PDB via DIA2Dump.exe.

All functions → raw RVA hooks (no parameter parsing).
Static functions included via RVA (not symbol).

Usage:
  python generate_hooks_from_pdb.py --module <dll> <pdb> [output.json]
  python generate_hooks_from_pdb.py --module <dll> --contain <s> <pdb> [output.json]
  python generate_hooks_from_pdb.py --module <dll> --dia2dump <path> <pdb> [output.json]
"""
import re
import json
import subprocess
import sys
from pathlib import Path


def find_dia2dump(override: str = "") -> str:
    if override:
        return override
    # Check same directory as this script
    local = Path(__file__).resolve().parent / "DIA2Dump.exe"
    if local.exists():
        return str(local)
    # Check toolbox
    toolbox = Path(r"C:\Users\zwsoft\toolbox\DIA2Dump.exe")
    if toolbox.exists():
        return str(toolbox)
    # Fallback: assume in PATH
    return "DIA2Dump.exe"


def run_dia2dump(dia2dump: str, pdb_path: str) -> str:
    result = subprocess.run(
        [dia2dump, "-s", pdb_path],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return result.stdout


def parse_functions(output: str, containing: str, module: str) -> list[dict]:
    hooks = []
    seen_rva = set()

    for line in output.splitlines():
        m = re.match(
            r"^\s*Function\s*:\s*(?:\w+)\s*,\s*\[([0-9A-Fa-f]+)\]\[[^\]]+\]\s*,\s*len\s*=\s*[0-9A-Fa-f]+\s*,\s*(.+)\s*$",
            line,
        )
        if not m:
            continue

        rva_hex = m.group(1)
        sig = m.group(2).strip()
        sig = re.sub(r"\s+", " ", sig)

        if containing and containing not in sig:
            continue

        rva = int(rva_hex, 16)
        if rva in seen_rva or rva == 0:
            continue
        seen_rva.add(rva)

        hooks.append({
            "module": module,
            "rva": f"0x{rva:x}",
            "action": "capture",
            "mode": "capture",
            "collector": "typed",
            "display_name": sig,
        })

    return hooks


def main():
    dia2dump_override = ""
    containing = ""
    module = ""
    output_path = "hooks_pdb.json"
    pdb_path = None
    args = sys.argv[1:]

    i = 0
    while i < len(args):
        if args[i] == "--dia2dump":
            i += 1
            dia2dump_override = args[i]
        elif args[i] == "--contain":
            i += 1
            containing = args[i]
        elif args[i] == "--module":
            i += 1
            module = args[i]
        elif args[i].startswith("--output="):
            output_path = args[i][9:]
        elif args[i].startswith("-"):
            print(f"Unknown flag: {args[i]}")
            sys.exit(1)
        elif pdb_path is None:
            pdb_path = args[i]
        elif output_path == "hooks_pdb.json":
            output_path = args[i]
        i += 1

    if not pdb_path or not module:
        print("Usage: generate_hooks_from_pdb.py --module <dll> [--contain <s>] [--dia2dump <path>] <pdb> [output.json]")
        print("")
        print("  --module <dll>    Target module name in hooks.json (required)")
        print("  --contain <s>     Only include functions whose name contains <s>")
        print("  --dia2dump <p>    Path to DIA2Dump.exe (default: DIA2Dump.exe)")
        print("  --output <file>   Output path (default: hooks_pdb.json)")
        sys.exit(1)

    dia2dump = find_dia2dump(dia2dump_override)
    print(f"Running DIA2Dump: {dia2dump} on {pdb_path}")
    out = run_dia2dump(dia2dump, pdb_path)

    hooks = parse_functions(out, containing, module)
    filter_info = f"(contain='{containing}')" if containing else "(no filter)"
    print(f"Found {len(hooks)} functions {filter_info}")

    config = {
        "output_dir": "dump_data",
        "output_format": "perfetto",
        "hooks": hooks,
    }

    Path(output_path).write_text(
        json.dumps(config, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    static_count = sum(1 for h in hooks if "static" in h.get("display_name", ""))
    export_count = len(hooks) - static_count
    print(f"  export: {export_count}, static: {static_count}")
    print(f"Written: {output_path}")


if __name__ == "__main__":
    main()
