"""Symbolize (module, RVA) call-sites captured in a Perfetto trace.

The hook framework records the *caller's* module + image-relative RVA for every
captured call as a nested debug annotation named "stack" (flattened by
trace_processor to `debug.stack.RVA` / `debug.stack.MOD`).  This tool reads an
already-captured `.perfetto-trace` file, resolves every RVA back to a source
code location using the module's PDB (via DIA2Dump), and produces:

  1. a symbolized report (JSON + human-readable text), and
  2. optionally an augmented copy of the trace with `FUNC` / `SRC` debug
     annotations injected so the resolved names show up in the Perfetto UI.

Usage:
  python symbolize_trace.py --trace trace.perfetto-trace \
      --pdb-config pdb_config.json \
      [--output report.json] [--augmented out.perfetto-trace]

The PDB config maps module name -> PDB path (basename match, case-insensitive):
  {
    "pdb_config": [
      { "module": "target.dll", "pdb_path": "C:/pdbs/target.pdb" }
    ]
  }
If no config entry matches, the tool falls back to <module_dir>/<basename>.pdb.
"""

import argparse
import csv
import io
import json
import re
import subprocess
import sys
from bisect import bisect_right
from pathlib import Path

DEFAULT_TOOLBOX = Path(r"C:\Users\zwsoft\toolbox")
PERFETTO_BIN_DIR = "perfetto/windows-amd64"

# DIA2Dump "-s" symbol line, e.g.:
#   Function : static, [000028B0][0001:000018B0], len = 0000000D, static int ...sig
SYMBOL_RE = re.compile(
    r"^\s*Function\s*:\s*(?:\w+)\s*,\s*\[([0-9A-Fa-f]+)\]\[[^\]]+\]\s*,"
    r"\s*len\s*=\s*([0-9A-Fa-f]+)\s*,\s*(.+?)\s*$"
)

# DIA2Dump "-l" line entry, e.g.:
#   \tline 21 at [000028B0][0001:000018B0], len = 0x5\tC:\...\file.cpp (0x3: HASH)
# The source file is only printed when it changes; later entries omit it.
LINE_RE = re.compile(
    r"^\s*line\s+(\d+)\s+at\s+\[([0-9A-Fa-f]+)\]\[[0-9A-Fa-f]+:[0-9A-Fa-f]+\]"
    r"\s*,\s*len\s*=\s*(?:0x)?[0-9A-Fa-f]+(?:\s+(.+))?$"
)
LINE_CHECKSUM_RE = re.compile(r"\s+\(0x[0-9A-Fa-f]+:\s*[0-9A-Fa-f]+\)\s*$")

# Max sane source line number (DIA sometimes emits garbage like 16707566).
MAX_LINE_NUMBER = 1_000_000

# The debug annotation carrying RVA/MOD, as flattened by trace_processor.
RVA_KEY = "debug.stack.RVA"
MOD_KEY = "debug.stack.MOD"


def find_tool(name, override, toolbox, subdir=None):
    if override:
        return override
    candidates = []
    if subdir:
        candidates.append(toolbox / subdir / name)
    candidates.append(toolbox / name)
    candidates.append(Path(__file__).resolve().parent / name)
    for p in candidates:
        if p.exists():
            return str(p)
    return name  # fall back to PATH lookup


def run(cmd, timeout=600):
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return proc.stdout, proc.stderr, proc.returncode


# ── trace extraction ────────────────────────────────────────────────────────

EXTRACT_SQL = (
    "SELECT s.id, s.ts, s.name, "
    f"(SELECT a.display_value FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='{RVA_KEY}') AS rva, "
    f"(SELECT a.display_value FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='{MOD_KEY}') AS mod "
    "FROM slice s "
    f"WHERE EXISTS (SELECT 1 FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='{RVA_KEY}') "
    "ORDER BY s.id"
)


def extract_callsites(trace, tp_shell):
    stdout, stderr, rc = run([tp_shell, "query", str(trace), EXTRACT_SQL])
    if rc != 0 and "Trace loaded" not in stderr and not stdout:
        raise RuntimeError(f"trace_processor query failed:\n{stderr}")

    rows = []
    reader = csv.DictReader(io.StringIO(stdout))
    for row in reader:
        if not row.get("rva") or not row.get("mod"):
            continue
        rows.append(
            {
                "ts": int(row["ts"]),
                "name": row["name"],
                "mod": row["mod"],
                "rva": int(row["rva"]),
            }
        )
    return rows


# ── PDB resolution ──────────────────────────────────────────────────────────

def load_pdb_config(config_path):
    """Return {module_basename_lower: pdb_path} from a JSON config file."""
    if not config_path:
        return {}
    data = json.loads(Path(config_path).read_text(encoding="utf-8"))
    mapping = {}
    for item in data.get("pdb_config", []):
        pdb = item.get("pdb_path") or item.get("pdb")
        if not pdb:
            continue
        mod = item.get("module")
        if mod:
            mapping[Path(mod).name.lower()] = pdb
    return mapping


def find_pdb(mod_path, pdb_map):
    base = Path(mod_path).name
    key = base.lower()
    if key in pdb_map:
        return pdb_map[key]
    candidate = Path(mod_path).with_suffix(".pdb")
    if candidate.exists():
        return str(candidate)
    return None


def dia2dump_symbols(dia2dump, pdb):
    stdout, stderr, rc = run([dia2dump, "-s", str(pdb)])
    symbols = []
    for line in stdout.splitlines():
        m = SYMBOL_RE.match(line)
        if not m:
            continue
        rva = int(m.group(1), 16)
        length = int(m.group(2), 16)
        sig = m.group(3).strip()
        symbols.append((rva, length, sig))
    symbols.sort(key=lambda x: x[0])
    return symbols


def dia2dump_lines(dia2dump, pdb):
    stdout, stderr, rc = run([dia2dump, "-l", str(pdb)])
    lines = []
    current_file = ""
    for line in stdout.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        lineno = int(m.group(1))
        rva = int(m.group(2), 16)
        if lineno <= 0 or lineno > MAX_LINE_NUMBER:
            continue
        rest = m.group(3)
        if rest:
            current_file = LINE_CHECKSUM_RE.sub("", rest).strip()
        if not current_file:
            continue
        lines.append((rva, current_file, lineno))
    lines.sort(key=lambda x: x[0])
    return lines


def cache_key(pdb):
    st = Path(pdb).stat()
    return f"{st.st_size}-{st.st_mtime_ns}"


class PdbResolver:
    def __init__(self, pdb_map, dia2dump, cache_dir, use_cache=True):
        self.pdb_map = pdb_map
        self.dia2dump = dia2dump
        self.cache_dir = Path(cache_dir)
        self.use_cache = use_cache
        self._symbols = {}
        self._lines = {}
        self._line_rvas = {}
        self._resolved = {}

    def _load(self, pdb):
        if pdb in self._symbols:
            return
        self._symbols[pdb] = []
        self._lines[pdb] = []
        self._line_rvas[pdb] = []

        if self.use_cache:
            cached = self.cache_dir / f"{Path(pdb).stem}-{cache_key(pdb)}.json"
            if cached.exists():
                try:
                    data = json.loads(cached.read_text(encoding="utf-8"))
                    self._symbols[pdb] = [tuple(x) for x in data.get("symbols", [])]
                    self._lines[pdb] = [tuple(x) for x in data.get("lines", [])]
                    self._line_rvas[pdb] = [x[0] for x in self._lines[pdb]]
                    return
                except Exception:
                    pass

        symbols = dia2dump_symbols(self.dia2dump, pdb)
        lines = dia2dump_lines(self.dia2dump, pdb)
        self._symbols[pdb] = symbols
        self._lines[pdb] = lines
        self._line_rvas[pdb] = [x[0] for x in lines]

        if self.use_cache:
            try:
                self.cache_dir.mkdir(parents=True, exist_ok=True)
                cached = self.cache_dir / f"{Path(pdb).stem}-{cache_key(pdb)}.json"
                cached.write_text(
                    json.dumps({"symbols": symbols, "lines": lines}),
                    encoding="utf-8",
                )
            except Exception:
                pass

    def resolve(self, mod_path, rva):
        key = (Path(mod_path).name.lower(), rva)
        if key in self._resolved:
            return self._resolved[key]

        pdb = find_pdb(mod_path, self.pdb_map)
        result = (None, None, None)
        if pdb and Path(pdb).exists():
            self._load(pdb)

            func = None
            symbols = self._symbols[pdb]
            if symbols:
                starts = [s[0] for s in symbols]
                i = bisect_right(starts, rva) - 1
                if i >= 0:
                    func = symbols[i][2]

            file_, lineno = None, None
            line_rvas = self._line_rvas[pdb]
            if line_rvas:
                i = bisect_right(line_rvas, rva) - 1
                if i >= 0:
                    _, file_, lineno = self._lines[pdb][i]

            result = (func, file_, lineno)

        self._resolved[key] = result
        return result


# ── text-proto augmentation ─────────────────────────────────────────────────

def proto_escape(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return "".join(out)


def proto_unescape(s):
    out = []
    i = 0
    simple = {"\\": "\\", '"': '"', "n": "\n", "r": "\r", "t": "\t"}
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            if nxt in simple:
                out.append(simple[nxt])
            else:
                out.append(nxt)
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


# Matches a debug_annotations block (body may contain single-level dict_entries).
DA_BLOCK_RE = re.compile(
    r"(?P<indent>[ \t]*)debug_annotations \{(?P<body>(?:[^{}]|\{[^{}]*\})*)\}",
    re.S,
)
RVA_IN_BLOCK_RE = re.compile(r'name:\s*"RVA"\s*uint_value:\s*(\d+)')
MOD_IN_BLOCK_RE = re.compile(r'name:\s*"MOD"\s*string_value:\s*"((?:[^"\\]|\\.)*)"')


def augment_text(text, resolver):
    count = 0

    def repl(match):
        nonlocal count
        indent = match.group("indent")
        body = match.group("body")
        rva_m = RVA_IN_BLOCK_RE.search(body)
        mod_m = MOD_IN_BLOCK_RE.search(body)
        if not rva_m or not mod_m:
            return match.group(0)

        rva = int(rva_m.group(1))
        mod = proto_unescape(mod_m.group(1))
        func, file_, lineno = resolver.resolve(mod, rva)
        if func is None and file_ is None:
            return match.group(0)

        pad = indent + "  "
        field = pad + "  "
        entries = ""
        if func is not None:
            entries += (
                f'\n{pad}dict_entries {{\n'
                f'{field}name: "FUNC"\n'
                f'{field}string_value: "{proto_escape(func)}"\n'
                f"{pad}}}"
            )
        if file_ is not None:
            src = f"{file_}:{lineno}"
            entries += (
                f'\n{pad}dict_entries {{\n'
                f'{field}name: "SRC"\n'
                f'{field}string_value: "{proto_escape(src)}"\n'
                f"{pad}}}"
            )
        count += 1
        return f"{indent}debug_annotations {{{body}{entries}\n{indent}}}"

    out = DA_BLOCK_RE.sub(repl, text)
    return out, count


def augment_trace(trace, output, resolver, traceconv, tp_shell):
    tmp_text = Path(str(output) + ".textproto")
    _, stderr, rc = run([traceconv, "text", str(trace), str(tmp_text)])
    if not tmp_text.exists() or tmp_text.stat().st_size == 0:
        raise RuntimeError(f"traceconv text failed:\n{stderr}")

    text = tmp_text.read_text(encoding="utf-8", errors="replace")
    new_text, injected = augment_text(text, resolver)
    if injected == 0:
        print("  [WARN] no stack annotations found to augment; keeping original trace")

    tmp_text.write_text(new_text, encoding="utf-8")
    _, stderr, rc = run([tp_shell, "convert", "binary", str(tmp_text), str(output)])
    tmp_text.unlink(missing_ok=True)
    if not Path(output).exists():
        raise RuntimeError(f"convert binary failed:\n{stderr}")
    return injected


# ── report ──────────────────────────────────────────────────────────────────

def write_report(entries, output_path):
    summary = {
        "total": len(entries),
        "resolved": sum(1 for e in entries if e["function"] or e["file"]),
        "unresolved": sum(1 for e in entries if not e["function"] and not e["file"]),
    }
    by_module = {}
    for e in entries:
        by_module.setdefault(Path(e["mod"]).name, 0)
        by_module[Path(e["mod"]).name] += 1

    doc = {"summary": summary, "modules": by_module, "entries": entries}
    out = Path(output_path)
    out.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  report JSON: {out}")

    txt = out.with_suffix(".txt")
    lines = [
        f"Resolved {summary['resolved']}/{summary['total']} call-sites "
        f"({summary['unresolved']} unresolved)",
        "",
    ]
    for e in entries:
        loc = f"{e['file']}:{e['line']}" if e["file"] else "<?>"
        func = e["function"] or "<unknown>"
        lines.append(
            f"  {e['ts']:>18}  {Path(e['mod']).name}!0x{e['rva']:x}  {func}  {loc}"
        )
    txt.write_text("\n".join(lines), encoding="utf-8")
    print(f"  report text: {txt}")


# ── main ────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Symbolize RVA call-sites in a Perfetto trace using PDBs.")
    ap.add_argument("--trace", required=True, help="input .perfetto-trace file")
    ap.add_argument("--pdb-config", help="JSON file mapping module -> PDB path")
    ap.add_argument("--output", help="output report JSON (default: <trace>.resolved.json)")
    ap.add_argument("--augmented", help="optional output path for the augmented trace")
    ap.add_argument("--toolbox", default=str(DEFAULT_TOOLBOX), help="toolbox root dir")
    ap.add_argument("--trace-processor", help="path to trace_processor_shell.exe")
    ap.add_argument("--traceconv", help="path to traceconv.exe")
    ap.add_argument("--dia2dump", help="path to DIA2Dump.exe")
    ap.add_argument("--cache-dir", default="./tmp_cache", help="PDB parse cache directory")
    ap.add_argument("--no-cache", action="store_true", help="disable PDB parse cache")
    args = ap.parse_args()

    trace = Path(args.trace)
    if not trace.exists():
        print(f"ERROR: trace not found: {trace}")
        return 1

    toolbox = Path(args.toolbox)
    tp_shell = find_tool("trace_processor_shell.exe", args.trace_processor, toolbox, PERFETTO_BIN_DIR)
    traceconv = find_tool("traceconv.exe", args.traceconv, toolbox, PERFETTO_BIN_DIR)
    dia2dump = find_tool("DIA2Dump.exe", args.dia2dump, toolbox)
    print(f"tools: tp={tp_shell}  traceconv={traceconv}  dia2dump={dia2dump}")

    pdb_map = load_pdb_config(args.pdb_config)
    if pdb_map:
        print(f"pdb config: {len(pdb_map)} module(s)")
    else:
        print("pdb config: none (co-located .pdb fallback only)")

    print("extracting call-sites from trace ...")
    entries = extract_callsites(trace, tp_shell)
    print(f"  found {len(entries)} call-sites")

    if not entries:
        print("Nothing to symbolize (no debug.stack.RVA annotations present).")
        return 0

    resolver = PdbResolver(pdb_map, dia2dump, args.cache_dir, use_cache=not args.no_cache)

    print("resolving RVAs ...")
    for e in entries:
        func, file_, lineno = resolver.resolve(e["mod"], e["rva"])
        e["function"] = func
        e["file"] = file_
        e["line"] = lineno
    resolved = sum(1 for e in entries if e["function"] or e["file"])
    print(f"  resolved {resolved}/{len(entries)}")

    output = Path(args.output) if args.output else trace.with_suffix(".resolved.json")
    write_report(entries, output)

    if args.augmented:
        print("augmenting trace ...")
        injected = augment_trace(trace, args.augmented, resolver, traceconv, tp_shell)
        print(f"  injected annotations into {injected} slice(s)")
        print(f"  augmented trace: {args.augmented}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
