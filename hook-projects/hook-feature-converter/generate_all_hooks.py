"""Generate hooks.json from Hex-Rays decompiled .c file for all functions.

Parses //----- (VA) -----  comments to extract function addresses.
Filters to .text section only.
"""
import json
import re
import sys
from pathlib import Path

IMAGE_BASE = 0x140000000
TEXT_START = 0x1000
TEXT_END = 0x2b7385  # .text section: VA=0x1000, size=0x2b6385
MODULE = "SLDFeatureConverter.exe"

C_PATH = Path(r"C:\Users\zwsoft\ws\sld_reversing\binaries\SLDFeatureConverter.exe.c")
OUT_DIR = Path(__file__).resolve().parent


def generate(limit: int = 0):
    hooks = []
    seen_rva = set()
    current_va = 0

    with open(C_PATH, errors="ignore") as f:
        for line in f:
            m = re.match(r"^//----- \(0([0-9A-Fa-f]+)\)", line)
            if m:
                current_va = int(m.group(1), 16)
                continue

            if current_va == 0:
                continue

            # Extract function signature
            sig = line.strip()
            if not sig or sig.startswith("//") or sig.startswith("/*"):
                continue

            rva = current_va - IMAGE_BASE
            current_va = 0

            if not (TEXT_START <= rva < TEXT_END):
                continue
            if rva in seen_rva:
                continue
            seen_rva.add(rva)

            hooks.append({
                "module": MODULE,
                "rva": f"0x{rva:x}",
                "action": "capture",
                "mode": "capture",
                "collector": "typed",
                "display_name": re.sub(r"\s+", " ", sig),
            })

            if limit and len(hooks) >= limit:
                break

    return hooks


def main():
    limit = 0
    out_path = OUT_DIR / "hooks_all.json"
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--max" and i + 1 < len(args):
            i += 1
            limit = int(args[i])
        elif args[i].startswith("--output="):
            out_path = Path(args[i][9:])
        i += 1

    hooks = generate(limit)
    config = {
        "output_dir": "dump_data",
        "output_format": "perfetto",
        "hooks": hooks,
    }
    out_path.write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Generated: {out_path}")
    print(f"  Hooks: {len(hooks)} (limit={limit or 'all'})")
    if hooks:
        rvas = [int(h["rva"], 16) for h in hooks]
        print(f"  RVA range: 0x{min(rvas):x} - 0x{max(rvas):x}")


if __name__ == "__main__":
    main()
