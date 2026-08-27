"""Extract the PDB path of each DLL/EXE under a directory via dumpbin /headers.

Parses the Debug Directories section of `dumpbin /headers` output and extracts
the PDB path recorded in the RSDS/NB10 debug entry. Emits a JSON config in the
same shape as tools/pdb_config_template.json:

    {
      "pdb_config": [
        { "module": "...", "pdb_path": "...",
          "function_white_list": [], "function_black_list": [] }
      ]
    }

Files without a debug directory are skipped.

Usage:
  python dump_pdb_path.py <dir> [--output pdb_config.json] [--dumpbin <path>]
  python dump_pdb_path.py <dir> --no-recursive
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

VSWHERE = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"

# RSDS line:  Format: RSDS, {GUID}, <age>, <pdb path>
# NB10  line:  Format: NB10, <offset>, <timestamp>, <age>, <pdb path>
PDB_RE = re.compile(r"Format:\s*(?:RSDS|NB10),\s*\{[0-9A-Fa-f-]+\},\s*[^,]+,\s*(.*)")


def find_dumpbin(explicit=None):
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return str(p)
        raise SystemExit(f"dumpbin not found at: {explicit}")

    found = shutil.which("dumpbin")
    if found:
        return found

    if Path(VSWHERE).is_file():
        try:
            out = subprocess.run(
                [VSWHERE, "-latest", "-products", "*",
                 "-find", r"VC/Tools/MSVC/**/bin/Hostx64/x64/dumpbin.exe"],
                capture_output=True, text=True,
            ).stdout.splitlines()
        except OSError:
            out = []
        for line in out:
            line = line.strip()
            if line and Path(line).is_file():
                return line

    raise SystemExit(
        "dumpbin not found. Provide --dumpbin <path> or run from a VS developer prompt."
    )


def collect_files(root: Path, recursive: bool):
    patterns = ("*.dll", "*.exe")
    files = []
    for pat in patterns:
        it = root.rglob(pat) if recursive else root.glob(pat)
        for p in it:
            if p.is_file():
                files.append(p)
    return sorted(set(files), key=lambda p: str(p).lower())


def extract_pdb(dumpbin: str, file: Path):
    try:
        proc = subprocess.run(
            [dumpbin, "/headers", str(file)],
            capture_output=True, text=True, errors="replace",
        )
    except OSError as e:
        return f"<error: {e}>"

    if proc.returncode != 0:
        err = proc.stderr.strip().splitlines()
        return f"<error: {'; '.join(err[-2:])}>" if err else f"<error: exit {proc.returncode}>"

    for line in proc.stdout.splitlines():
        m = PDB_RE.search(line)
        if m:
            return m.group(1).strip()
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", help="Directory to scan for .dll/.exe files")
    ap.add_argument("--output", default="pdb_config.json", help="Output JSON path")
    ap.add_argument("--dumpbin", help="Explicit path to dumpbin.exe")
    ap.add_argument("--no-recursive", action="store_true", help="Only scan top level")
    args = ap.parse_args()

    root = Path(args.dir)
    if not root.is_dir():
        raise SystemExit(f"Not a directory: {root}")

    dumpbin = find_dumpbin(args.dumpbin)
    files = collect_files(root, not args.no_recursive)

    results = []
    for f in files:
        pdb = extract_pdb(dumpbin, f)
        if pdb and not pdb.startswith("<error"):
            results.append({
                "module": str(f),
                "pdb_path": pdb,
                "function_white_list": [],
                "function_black_list": [],
            })

    out = Path(args.output)
    out.write_text(
        json.dumps({"pdb_config": results}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(f"Scanned {len(files)} files, {len(results)} with PDB, written to {out}")


if __name__ == "__main__":
    main()
