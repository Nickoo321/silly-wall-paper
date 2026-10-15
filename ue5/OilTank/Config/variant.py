# usage: python variant.py out.json key=value ...   (values parsed as JSON, else string)
import json, sys, re
src = open('OilTank.json', encoding='utf-8-sig').read()
cfg = json.loads(src)
for kv in sys.argv[2:]:
    k, v = kv.split('=', 1)
    try: v = json.loads(v)
    except Exception: pass
    if k.startswith('cvars.'): cfg['cvars'][k[6:]] = str(v)
    else: cfg[k] = v
json.dump(cfg, open(sys.argv[1], 'w'), indent=1)
