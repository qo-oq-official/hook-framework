import json
from pathlib import Path

path = Path(r'C:\Users\zwsoft\hook-framework\hook-projects\hook-swccu\hooks.json')
data = json.loads(path.read_text(encoding='utf-8'))

changed = 0
for hook in data['hooks']:
    sym = hook.get('symbol', '')
    if 'su_CArchive' not in sym:
        continue
    for param in hook.get('params', []):
        if isinstance(param, dict) and param.get('name') in ('this', 'archive') and param.get('type') == 'pointer':
            param['type'] = 'su_CArchivePtr'
            changed += 1

path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
print(f'Updated {changed} params to su_CArchivePtr')

# verify
data2 = json.loads(path.read_text(encoding='utf-8'))
hooks = [h for h in data2['hooks'] if any(
    p.get('type') == 'su_CArchivePtr' and p.get('name') in ('this','archive')
    for p in h.get('params',[]) if isinstance(p, dict)
)]
print(f'Verified: {len(hooks)} hooks with su_CArchivePtr param')
print(f'Total hooks: {len(data2["hooks"])}')
