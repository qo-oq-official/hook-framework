"""Generate hooks.json from PDB via DIA2Dump.exe.

Reads each PDB listed in a JSON config, dumps function symbols with DIA2Dump
(`-s`), and emits a hooks.json with RVA-based hooks (mode=trace,
collector=typed). Static functions are included by RVA as well (they have no
export symbol), so every function can be hooked.

Usage:
  python generate_hooks_from_pdb.py --config <config.json> [--output=<file>]
  python generate_hooks_from_pdb.py --config <config.json> [--dia2dump <path>]

Options:
  --config <file>     JSON config listing the PDBs to parse (required). Each
                      entry of "pdb_config" must provide: pdb_path, module,
                      function_white_list, function_black_list.
  --dia2dump <path>   Path to DIA2Dump.exe (default: auto-detected).
  --output=<file>     Output path (default: hooks_pdb.json). The output path can
                      also be given as a positional argument.

Example config (config.json):
  {
    "pdb_config": [
      {
        "module": "target.dll",
        "pdb_path": "C:/pdbs/target.pdb",
        "function_white_list": [".+"],
        "function_black_list": []
      }
    ]
  }
"""
import argparse
import re
import json
import subprocess
from pathlib import Path
from dataclasses import dataclass
import hashlib

@dataclass
class PdbParserConfig:
    pdb_path: str
    module: str
    function_white_list: list[str]
    function_black_list: list[str]

@dataclass
class ParserConfig:
    pdb_config: list[PdbParserConfig]

    def __post_init__(self):
        if isinstance(self.pdb_config, list):
            self.pdb_config = [
                (
                    PdbParserConfig(**item)
                    if isinstance(item, dict)
                    else item
                )
                for item in self.pdb_config
        ]

    @classmethod
    def from_json_file(cls, file_path: str):
        """解析 JSON 文件并填充配置类"""
        with open(file_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return cls(**data)


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


def get_pdb_hash(pdb_path: str):
    with open(pdb_path, "rb") as f:
        # Calculate the SHA-256 hash (or use hashlib.md5, hashlib.sha1, etc.)
        digest = hashlib.file_digest(f, hashlib.sha256)
        return digest

def get_pdb_caceh_filename(pdb_path: str):
    return f"{Path(pdb_path).stem}-{get_pdb_hash(pdb_path).hexdigest()}-pdb-cache"


def get_cache_file_content(cache_filename: str):
    res = []
    with open(cache_filename, "r") as file:
        for line in file:
            sig, rva = line.strip().split("||")
            res.append([sig, int(rva)])
    return res


def caceh_dia2dump_result(caceh_file: Path, function_line: list[(str, str)]):
    with open(caceh_file, "w+") as file:
        for sig, rva in function_line:
            file.write(f"{sig}||{rva}\n")

def run_dia2dump(dia2dump: str, pdb_path: str) -> str:
    result = subprocess.run(
        [dia2dump, "-s", pdb_path],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return result.stdout


def parse_functions(output: str, cache_file: str) -> list[(str, str)]:
    cache_list = []
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
        rva = int(rva_hex, 16)
        cache_list.append([sig, rva])

    caceh_dia2dump_result(cache_file, cache_list)
    return cache_list


def build_hook_config(rva_list, function_white_list: list[str], function_black_list: list[str], module: str) -> list[dict]:
    hooks = []
    seen_rva = set()
    for sig, rva in rva_list:
        hit = False
        if function_white_list:
            for pattern in function_white_list:
                if re.match(pattern, sig):
                    hit = True
                    break
        if hit and function_black_list:
            for pattern in function_black_list:
                if re.match(pattern, sig):
                    hit = False
                    break
        if not hit:
            continue

        if rva in seen_rva or rva == 0:
            print(f"skip {sig=} {rva=}")
            continue
        seen_rva.add(rva)

        hooks.append({
            "module": module,
            "rva": f"0x{rva:x}",
            "action": "capture",
            "mode": "trace",
            "collector": "typed",
            "display_name": sig,
        })

    return hooks


def main():
    ap = argparse.ArgumentParser(
        description="Generate hooks.json from PDB via DIA2Dump.exe.",
    )
    ap.add_argument("--config", required=True,
                    help="JSON config listing the PDBs to parse")
    ap.add_argument("--dia2dump", default="",
                    help="Path to DIA2Dump.exe (default: auto-detected)")
    ap.add_argument("--output", default=None,
                    help="Output path (default: hooks_pdb.json)")
    ap.add_argument("output_pos", nargs="?", default=None,
                    help="Output path (positional alternative to --output)")
    args = ap.parse_args()

    config_file = args.config
    dia2dump_override = args.dia2dump
    output_path = args.output or args.output_pos or "hooks_pdb.json"

    if not (Path(output_path).parent / "tmp_cache").exists():
        (Path(output_path).parent / "tmp_cache").mkdir()
    
    config = ParserConfig.from_json_file(config_file)
    hooks = []
    for pdb_config in config.pdb_config:
        module = Path(pdb_config.module).name
        cache_filename = Path(output_path).parent / "tmp_cache" / get_pdb_caceh_filename(pdb_config.pdb_path)
        rva_list = []
        if not cache_filename.exists():
            dia2dump = find_dia2dump(dia2dump_override)
            print(f"Running DIA2Dump: {dia2dump} on {pdb_config.pdb_path}")
            out = run_dia2dump(dia2dump, pdb_config.pdb_path)
            rva_list = parse_functions(out, cache_filename)
        else:
            print(f"Read PDB cache: {pdb_config.pdb_path} {cache_filename}")
            rva_list = get_cache_file_content(cache_filename)
        hooks += build_hook_config(rva_list, 
                                   pdb_config.function_white_list,
                                   pdb_config.function_black_list,
                                   module)
        print(f"Found {len(hooks)} functions")

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
