"""Parse SOLIDWORKS TLB to extract COM interface vtable metadata using comtypes."""
import sys, json, re
from pathlib import Path

TLB_PATH = r'C:\Program Files\SOLIDWORKS Corp 2024\SOLIDWORKS\sldworks.tlb'
OUT_PATH = Path(r'C:\Users\zwsoft\hook-framework\hook-projects\hook-ipx\ipx_interfaces.json')

VT_LOOKUP = {
    2: 'I2', 3: 'I4', 4: 'R4', 5: 'R8', 8: 'BSTR', 11: 'BOOL',
    12: 'VARIANT', 13: 'UNKNOWN', 16: 'I1', 17: 'UI1', 18: 'UI2',
    19: 'UI4', 20: 'I8', 21: 'UI8', 22: 'INT', 23: 'UINT',
    24: 'VOID', 25: 'HRESULT', 26: 'PTR', 27: 'SAFEARRAY',
    30: 'LPSTR', 31: 'LPWSTR',
}

VT_TO_PARAM = {
    2: 'int16', 3: 'int32', 4: 'float32', 5: 'float64',
    8: 'bstr', 11: 'int8', 12: 'variant', 13: 'pointer',
    16: 'int8', 17: 'uint8', 18: 'uint16', 19: 'uint32',
    20: 'int64', 21: 'uint64', 22: 'int32', 23: 'uint32',
    25: 'int32', 26: 'pointer', 27: 'safearray',
    30: 'cstring', 31: 'wstring',
}

# ctypes type → VT
def guess_vt(ct):
    name = getattr(ct, '__name__', str(ct))
    if 'BSTR' in name: return 8
    if 'LP_POINTER' in name or 'POINTER' in name: return 26
    if 'c_long' in name: return 3
    if 'c_short' in name: return 2
    if 'c_byte' in name: return 16
    if 'c_float' in name: return 4
    if 'c_double' in name: return 5
    if 'VARIANT' in name: return 12
    if 'IDispatch' in name: return 9
    if 'IUnknown' in name: return 13
    if 'SAFEARRAY' in name: return 27
    if 'c_bool' in name: return 11
    return 26  # default pointer


def main():
    import comtypes.client
    mod = comtypes.client.GetModule(TLB_PATH)

    interfaces = {}
    ignore = {'_check_version', '_lcid', 'tag', 'Library'}
    
    for name in sorted(dir(mod)):
        if name.startswith('_') or name in ignore:
            continue
        obj = getattr(mod, name)
        if not isinstance(obj, type):
            continue
        
        # Check if it has _iid_ (COM interface GUID)
        if not hasattr(obj, '_iid_'):
            continue
        iid = str(obj._iid_)
        if iid == '00000000-0000-0000-0000-000000000000':
            continue
            
        # Check for _methods_ (COM interface methods)
        if not hasattr(obj, '_methods_'):
            continue
            
        methods = []
        for i, spec in enumerate(obj._methods_):
            vtable_idx = i  # Position in _methods_ IS the vtable index for dual/custom interfaces
            mname = spec.name
            
            # Parse argtypes
            argtypes = spec.argtypes if spec.argtypes else ()
            paramflags = spec.paramflags if spec.paramflags else ()
            
            params = []
            param_names = []
            for pf in paramflags:
                if isinstance(pf, tuple) and len(pf) >= 2:
                    param_names.append(pf[1])
            
            for ai, at in enumerate(argtypes):
                pname = param_names[ai] if ai < len(param_names) else f'arg{ai}'
                vt = guess_vt(at)
                params.append({
                    'name': pname,
                    'vt': vt,
                    'vt_name': VT_LOOKUP.get(vt, f'VT_{vt}'),
                    'hook_type': VT_TO_PARAM.get(vt, 'pointer'),
                })
            
            # Return type
            restype = spec.restype
            ret_vt = guess_vt(restype) if restype else 25
            
            methods.append({
                'name': mname,
                'vtable_index': vtable_idx,
                'params': params,
                'return_vt': ret_vt,
                'return_vt_name': VT_LOOKUP.get(ret_vt, 'HRESULT'),
            })

        if methods:
            interfaces[name] = {
                'iid': iid,
                'method_count': len(methods),
                'methods': methods,
            }

    print(f'Found {len(interfaces)} COM interfaces')
    top = sorted(interfaces.items(), key=lambda x: x[1]['method_count'], reverse=True)
    for name, info in top[:10]:
        print(f'  {name}: {info["method_count"]} methods, IID={info["iid"]}')
    
    total = sum(i['method_count'] for i in interfaces.values())
    print(f'Total: {total} methods across {len(interfaces)} interfaces')

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(interfaces, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'Saved to {OUT_PATH}')


if __name__ == '__main__':
    main()
