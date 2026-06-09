#!/usr/bin/env python3
"""Verify hook framework output correctness."""

import os
import sys
import json
from pathlib import Path


def verify_dump_data(dump_dir: str) -> bool:
    dump_path = Path(dump_dir)
    if not dump_path.exists():
        print(f"ERROR: dump_data directory not found: {dump_dir}")
        return False

    errors = []
    total_files = 0
    raw_files = 0
    json_files = 0

    for root, dirs, files in os.walk(dump_path):
        for f in files:
            total_files += 1
            fpath = Path(root) / f

            if f.endswith(".raw"):
                raw_files += 1
                size = fpath.stat().st_size
                if size == 0:
                    errors.append(f"Empty raw file: {fpath}")
                elif size < 100:
                    errors.append(f"Suspiciously small raw file ({size}B): {fpath}")

            elif f.endswith(".json"):
                json_files += 1
                try:
                    with open(fpath, "r", encoding="utf-8") as jf:
                        data = json.load(jf)
                    if not isinstance(data, dict):
                        errors.append(f"JSON not an object: {fpath}")
                except json.JSONDecodeError as e:
                    errors.append(f"Invalid JSON in {fpath}: {e}")

    print("=== Verification Results ===")
    print(f"Total files: {total_files}")
    print(f"Raw files:   {raw_files}")
    print(f"JSON files:  {json_files}")
    print(f"Errors:      {len(errors)}")

    if errors:
        print("\nERRORS:")
        for e in errors:
            print(f"  - {e}")
        return False

    if total_files == 0:
        print("\nWARNING: No output files found!")
        return False

    print("\nAll checks passed!")
    return True


def main():
    dump_dir = sys.argv[1] if len(sys.argv) > 1 else "dump_data"
    success = verify_dump_data(dump_dir)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
