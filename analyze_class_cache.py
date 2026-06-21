"""Analyze class cache indices from hook trace data."""
import json, sys, os

def parse_trace(path):
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    return data.get('traceEvents', data) if isinstance(data, dict) else data

def analyze(trace_path):
    events = parse_trace(trace_path)

    # Pair B/E by sequential matching within same tid for ReadClass/WriteClass
    pending = {}  # tid -> name -> entry event
    pairs = []

    for e in events:
        name = e.get('name', '')
        if 'ReadClass' not in name and 'WriteClass' not in name:
            continue
        ph = e.get('ph')
        tid = e.get('tid', 0)

        if ph == 'B':
            # use tid+name as key
            pending[(tid, name)] = e
        elif ph == 'E':
            key = (tid, name)
            if key in pending:
                pairs.append((pending.pop(key), e))

    # Track class indices per archive
    archives = {}

    for be, ee in pairs:
        name = be.get('name', '')
        args_be = be.get('args', {})
        args_ee = ee.get('args', {})
        archive = args_be.get('this', '?')
        ts = be.get('ts', 0)

        if archive not in archives:
            archives[archive] = {'counter': 1, 'classes': {}, 'lookups': []}

        a = archives[archive]

        if 'WriteClass' in name:
            cls = args_be.get('pClassName', '?')
            idx = a['counter']
            a['classes'][idx] = cls
            a['counter'] = idx + 1
            a['lookups'].append((ts, f'W idx={idx:>3}  {cls}'))

        elif 'ReadClass' in name:
            cls = args_be.get('pClassName', '?')
            tag = args_ee.get('pTag_out', 0)

            # 2147516415 = 0x7FFEFFFF = full class header tag
            if tag == 2147516415:
                idx = a['counter']
                a['classes'][idx] = cls
                a['counter'] = idx + 1
                a['lookups'].append((ts, f'R idx={idx:>3}  {cls}'))
            elif tag & 0x80000000:
                idx = tag & 0x7FFF
                cached = a['classes'].get(idx, '???')
                a['lookups'].append((ts, f'L idx={idx:>3}  -> {cached}'))

    return archives

def main():
    if len(sys.argv) < 2:
        import glob
        files = sorted(glob.glob(r'C:\Users\zwsoft\hook-framework\hook-projects\hook-swccu\dump_data\trace_*.json'))
        if not files:
            print("No trace files found")
            return 1
        path = files[-1]
    else:
        path = sys.argv[1]

    print(f"Analyzing: {os.path.basename(path)}\n")
    archives = analyze(path)

    shown = 0
    for arch_addr, arch in sorted(archives.items(), key=lambda a: max(l[0] for l in a[1]['lookups']) if a[1]['lookups'] else 0):
        if not arch['lookups']:
            continue
        shown += 1
        print(f"=== Archive {arch_addr} ({len(arch['classes'])} classes) ===")
        for ts, line in sorted(arch['lookups']):
            print(f"  {line}")
        print()

    if shown == 0:
        print("No class registrations found.")
    else:
        print(f"Shown {shown} archive(s).")

    # Summary: count lookups by type per archive
    print("\n=== SUMMARY ===")
    for arch_addr in sorted(archives):
        arch = archives[arch_addr]
        wr = sum(1 for _, l in arch['lookups'] if l.startswith('W'))
        rd = sum(1 for _, l in arch['lookups'] if l.startswith('R'))
        lk = sum(1 for _, l in arch['lookups'] if l.startswith('L'))
        if wr or rd or lk:
            print(f"  {arch_addr:>14s}  W:{wr} R:{rd} L:{lk}")

if __name__ == '__main__':
    sys.exit(main() or 0)
