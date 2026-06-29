"""Add capture_on_exit=true to su_CArchivePtr params in hooks.json."""
import json
from pathlib import Path

path = Path(r'C:\Users\zwsoft\hook-framework\hook-projects\hook-swccu\hooks.json')
data = json.loads(path.read_text(encoding='utf-8'))

changed = 0
for hook in data['hooks']:
    for param in hook.get('params', []):
        if isinstance(param, dict) and param.get('type') == 'su_CArchivePtr' and not param.get('capture_on_exit'):
            param['capture_on_exit'] = True
            changed += 1

path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
print(f'Updated {changed} su_CArchivePtr params with capture_on_exit=true')

# verify
data2 = json.loads(path.read_text(encoding='utf-8'))
total = 0
with_cexit = 0
for hook in data2['hooks']:
    for p in hook.get('params', []):
        if isinstance(p, dict) and p.get('type') == 'su_CArchivePtr':
            total += 1
            if p.get('capture_on_exit'):
                with_cexit += 1
print(f'Verified: {with_cexit}/{total} su_CArchivePtr params have capture_on_exit')
print(f'Total hooks: {len(data2["hooks"])}')