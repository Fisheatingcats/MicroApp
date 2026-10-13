#!/usr/bin/env python3
"""Inspect and validate MicroApp 2.0 metadata, body, fixups and resources."""
import argparse
import json
from pathlib import Path
from microapp_profile import load_profile
from microapp_wire import validate
def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('package',type=Path)
    p.add_argument('--profile',type=Path); p.add_argument('--json',action='store_true'); a=p.parse_args()
    h,m,_=validate(a.package.read_bytes(),load_profile(a.profile) if a.profile else None)
    print(json.dumps(m,indent=2) if a.json else f"OK 2.0 {m['app_id']} {m['version']}: Flash {m['body_size']} RAM {m['ram_size']} fixups {m['fixup_count']}")
    return 0
if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,KeyError,OSError) as e: raise SystemExit(str(e))
