#!/usr/bin/env python3
"""Parse Perfetto trace JSON to reconstruct SW entity nesting and fields.

Input:  Perfetto trace JSON from hook_framework injection (swccu.dll hooks).
Output: Entity call tree with nested Serialize chains + field reads.
        Class cache timeline per archive (index → class name).

Usage:  python analyze_entity_tree.py [trace.json]
        python analyze_entity_tree.py --cache-only [trace.json]
"""

import json, sys, os, glob
from collections import defaultdict

def parse(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        data = json.load(f)
    return data.get('traceEvents', data) if isinstance(data, dict) else data

def find_fast(events, key, val):
    """Quick lookup: does any event have this value in args?"""
    s = json.dumps({'k': val})
    return [e for e in events if s in json.dumps(e.get('args', {}))]

# ── 1. Pair B/E events ────────────────────────────────────────────────

def pair_events(events, target_names=None):
    """Sequential B/E pairing within same (pid, tid, name)."""
    pending = {}       # (pid, tid, name) → begin_event
    pairs = []

    for e in events:
        name = e.get('name', '')
        if target_names and not any(t in name for t in target_names):
            continue
        ph = e.get('ph', '')
        pid = e.get('pid', 0)
        tid = e.get('tid', 0)
        key = (pid, tid, name)

        if ph == 'B':
            pending[key] = e
        elif ph == 'E' and key in pending:
            pairs.append((pending.pop(key), e))
    return pairs

# ── 2. Build entity hierarchy from ReadObject/ReadClass nesting ───────

def build_entity_tree(pairs, events):
    """Build entity call tree: {class, fields, children}."""
    # Collect all events by pid/tid for nesting
    by_tid = defaultdict(list)
    for e in events:
        tid = e.get('tid', 0)
        by_tid[tid].append(e)

    trees = defaultdict(list)  # archive → list of root entities

    for tid, tid_events in by_tid.items():
        tid_events.sort(key=lambda e: e.get('ts', 0))
        stack = []  # current nesting: [{phase, class, fields, children, ts_begin}]

        for e in tid_events:
            name = e.get('name', '')
            args = e.get('args', {})
            ph = e.get('ph', '')
            ts = e.get('ts', 0)
            archive = args.get('arg0', args.get('this', args.get('archive', '')))

            # Entity boundary: ReadObject B or ReadClass B
            if ph == 'B' and 'ReadObject' in name:
                stack.append({
                    'ts': ts, 'archive': archive,
                    'class': '?', 'fields': [], 'children': [],
                    'type': 'entity', 'events': [e]
                })
            elif ph == 'E' and 'ReadObject' in name and stack:
                node = stack.pop()
                node['events'].append(e)
                cc = args.get('class_cache_after', {})
                if cc:
                    node['class'] = cc.get('class', '?')
                # Attach to parent
                pClass = e.get('args', {}).get('return', '')
                if stack:
                    stack[-1]['children'].append(node)
                else:
                    key = archive or 'root'
                    trees[key].append(node)

            # Field read: operator>>(type &) B/E
            elif ph == 'B' and 'operator>>' in name and ('su_CArchive::operator>>' in name or 'su_CArchive & operator>>' in name):
                if stack:
                    stack[-1]['events'].append(e)
                    # Try to capture field value from exit
            elif ph == 'E' and 'operator>>' in name and stack:
                stack[-1]['events'].append(e)
                val = args.get('pValue_out', args.get('return', None))
                if val is not None:
                    field = {
                        'name': name.split('::')[-1].split('(')[0].strip(),
                        'value': val,
                        'ts': ts
                    }
                    stack[-1]['fields'].append(field)

            # Serialize method entry/exit
            elif ph == 'B' and 'Serialize' in name:
                if stack:
                    stack[-1]['events'].append(e)
            elif ph == 'E' and 'Serialize' in name and stack:
                stack[-1]['events'].append(e)
                pClass = e.get('args', {}).get('return', '')
                pass

    return dict(trees)

# ── 3. Extract class cache timeline ───────────────────────────────────

def extract_class_cache(pairs):
    """From ReadClass B/E pairs, rebuild class→index mapping per archive."""
    archives = defaultdict(lambda: {'counter': 0, 'classes': {}, 'timeline': []})

    for be, ee in pairs:
        name = be.get('name', '')
        if 'ReadClass' not in name:
            continue

        args_be = be.get('args', {})
        args_ee = ee.get('args', {})
        archive = args_be.get('arg0', args_be.get('this', '?'))
        cc = args_ee.get('class_cache_after', {})
        if not cc:
            continue

        counter = cc.get('counter', 0)
        cls_name = cc.get('class', '?')
        ts = ee.get('ts', 0)

        a = archives[archive]
        a['counter'] = counter
        if cls_name and cls_name != '?' and cls_name != '<?>':
            # Index = counter - 1 (counter is POST-operation)
            idx = max(1, counter - 1) if counter > 1 else 1
            a['classes'][idx] = cls_name
            a['timeline'].append((ts, idx, cls_name))

    return dict(archives)

# ── 4. Main ───────────────────────────────────────────────────────────

def main():
    cache_only = '--cache-only' in sys.argv

    if len(sys.argv) < 2 or sys.argv[-1].startswith('--'):
        files = sorted(glob.glob(
            r'C:\Users\zwsoft\hook-framework\hook-projects\hook-swccu\dump_data\trace_*.json'))
        if not files:
            print("No trace files found.")
            return 1
        path = files[-1]
    else:
        path = sys.argv[-1]

    if not os.path.exists(path):
        print(f"File not found: {path}")
        return 1

    print(f"Loading: {os.path.basename(path)} ({os.path.getsize(path)//1024}KB)")
    events = parse(path)
    print(f"Events: {len(events)}")

    # ── Pair ReadClass events for class cache ──
    rc_pairs = pair_events(events, ['ReadClass'])
    archives = extract_class_cache(rc_pairs)

    print(f"\n=== Class Cache ({len(archives)} archives) ===\n")
    for ar in sorted(archives, key=lambda a: min(archives[a]['classes'].keys(), default=0)):
        a = archives[ar]
        if not a['classes']:
            continue
        print(f"Archive {ar}:")
        for idx in sorted(a['classes']):
            print(f"  idx={idx:>4}  class={a['classes'][idx]}")
        print()

    if cache_only:
        return 0

    # ── Build entity tree ──
    all_pairs = pair_events(events)
    trees = build_entity_tree([p for p in all_pairs if 'ReadObject' in p[0].get('name','')], events)

    print(f"\n=== Entity Trees ({len(trees)} archives) ===")
    def dump_tree(nodes, depth=0):
        for node in nodes:
            cls = node.get('class', '?')
            fields = node.get('fields', [])
            indent = '  ' * depth
            print(f"{indent}class={cls}  fields={len(fields)}  children={len(node.get('children',[]))}")
            for ch in node.get('children', []):
                dump_tree([ch], depth + 1)

    for ar, nodes in trees.items():
        if not nodes:
            continue
        print(f"\nArchive {ar}:")
        dump_tree(nodes)
        break  # just first one for now

    # ── Summary: operator>> field reads ──
    print(f"\n=== Field Reads (operator>>) ===")
    field_count = defaultdict(int)
    for e in events:
        name = e.get('name', '')
        if 'operator>>' in name and e.get('ph') == 'B' and 'su_CArchive' in name:
            sig = name.split('@')[0].strip()
            # Extract type: operator>>(type &) or operator>>(type)
            if 'int &' in sig or 'long &' in sig:
                typ = 'int'
            elif 'unsigned long &' in sig or 'unsigned int &' in sig:
                typ = 'uint'
            elif 'double &' in sig or 'float' in sig:
                typ = 'double'
            elif 'CStringT' in sig or 'ATL::CStringT' in sig:
                typ = 'wstring'
            elif 'CObject' in sig:
                typ = 'entity*'
            else:
                typ = 'other'
            field_count[typ] += 1

    for typ, cnt in sorted(field_count.items(), key=lambda x: -x[1]):
        bar = '█' * min(50, cnt // 50)
        print(f"  {typ:>10s}: {cnt:>6d}  {bar}")

    return 0

if __name__ == '__main__':
    sys.exit(main() or 0)
