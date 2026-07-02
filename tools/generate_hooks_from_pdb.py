"""Generate hooks.json from PDB via DIA2Dump.exe.

All functions → raw RVA hooks (no parameter parsing).
Static functions included via RVA (not symbol).
Filter: only include functions matching --prefix pattern.
"""
import re
import json
import subprocess
import sys
from pathlib import Path

DIA2DUMP = r"C:\Users\zwsoft\toolbox\DIA2Dump.exe"


def run_dia2dump(pdb_path: str) -> str:
    result = subprocess.run(
        [DIA2DUMP, "-s", pdb_path],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return result.stdout


def parse_functions(output: str, prefix: str = "") -> list[dict]:
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

        if prefix and not any(part.startswith(prefix) for part in sig.split()):
            continue

        rva = int(rva_hex, 16)
        if rva in seen_rva or rva == 0:
            continue
        seen_rva.add(rva)

        hooks.append({
            "module": "test_pdb_target.dll",
            "rva": f"0x{rva:x}",
            "action": "capture",
            "mode": "capture",
            "collector": "raw",
            "display_name": sig,
        })

    return hooks


def main():
    prefix = "pdb_"
    pdb_path = None
    output_path = "hooks_pdb.json"
    args = sys.argv[1:]

    i = 0
    while i < len(args):
        if args[i] == "--prefix":
            i += 1
            prefix = args[i]
        elif args[i] == "--all":
            prefix = ""
        elif args[i].startswith("--output="):
            output_path = args[i][9:]
        elif pdb_path is None:
            pdb_path = args[i]
        i += 1

    if not pdb_path:
        print(f"Usage: {sys.argv[0]} [--prefix pdb_] [--all] <pdb_path>")
        sys.exit(1)

    print(f"Running DIA2Dump on: {pdb_path}")
    out = run_dia2dump(pdb_path)

    hooks = parse_functions(out, prefix)
    print(f"Found {len(hooks)} functions (prefix='{prefix or '(all)'}')")

    config = {
        "output_dir": "dump_data",
        "output_format": "file",
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
