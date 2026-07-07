"""Generate hooks.json for SLDFeatureConverter.exe from CSV.

Input: entity_driver_deserialize.csv (class + deser_addr columns)
Output: hooks.json (804 typed RVA hooks, Perfetto format)
"""
import csv
import json
import sys
from pathlib import Path

IMAGE_BASE = 0x140000000
MODULE = "SLDFeatureConverter.exe"
CSV_PATH = Path(__file__).resolve().parent / "entity_driver_deserialize.csv"


def generate(csv_path: Path, output_path: Path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))

    hooks = []
    for r in rows:
        addr_str = r.get("deser_addr", "").strip()
        if not addr_str:
            continue
        addr = int(addr_str, 16)
        rva = addr - IMAGE_BASE
        hooks.append({
            "module": MODULE,
            "rva": f"0x{rva:x}",
            "action": "capture",
            "mode": "capture",
            "collector": "typed",
            "display_name": r["class"].strip(),
        })

    config = {
        "output_dir": "dump_data",
        "output_format": "perfetto",
        "hooks": hooks,
    }

    output_path.write_text(
        json.dumps(config, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    print(f"Generated: {output_path}")
    print(f"  Hooks: {len(hooks)}")
    print(f"  ImageBase: 0x{IMAGE_BASE:x}")
    print(f"  RVA range: 0x{min(int(h['rva'],16) for h in hooks):x} - 0x{max(int(h['rva'],16) for h in hooks):x}")


if __name__ == "__main__":
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else CSV_PATH
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent / "hooks.json"
    generate(csv_path, out_path)
