#!/usr/bin/env python3
"""Audit ET_DYN and package immutable code + instance RAM initialization."""
import argparse
import json
from pathlib import Path
from microapp_profile import load_profile
from microapp_elf import extract_elf
from microapp_wire import HEADER, MANIFEST, FIXUP, MAGIC, encode_manifest, resource_blob, align, crc, validate

def pack(elf,profile,metadata,resources=()):
    x=extract_elf(elf,profile['float_abi'])
    permissions=metadata.get('permissions',[])
    if any(p not in profile.get('permissions',{}) for p in permissions):
        raise ValueError('unknown permission')
    m={k:metadata.get(k,'') for k in ('app_id','name','version','author','description')}
    if any(not m[k] for k in ('app_id','name','version')): raise ValueError('manifest requires app_id/name/version')
    m.update(board_id=profile['board_id'], abi_major=profile['abi_major'],abi_minor=profile['abi_minor'],
             min_host_version=profile['host_version'],permission_mask=sum(profile['permissions'][p] for p in set(permissions)),
             architecture=1,float_abi=('soft','softfp','hard').index(profile['float_abi']))
    body=bytearray(align(x['image'])); m.update(image_size=len(x['image']),image_crc32=crc(x['image']))
    m.update({k:x[k] for k in ('entry_offset','image_alignment','ram_size','ram_alignment','got_offset','got_size','bss_offset','bss_size','data_size')})
    m.update(init_offset=len(body),init_size=len(x['initial'])); body.extend(align(x['initial']))
    m.update(fixup_offset=len(body),fixup_count=len(x['fixups']))
    for f in x['fixups']: body.extend(FIXUP.pack(*f))
    res=resource_blob(resources); m.update(resource_offset=len(body),resource_size=len(res),resource_crc32=crc(res)); body.extend(res)
    m.update(body_size=len(body),body_crc32=crc(body))
    off=HEADER.size+MANIFEST.size; total=off+len(body)
    fields=[MAGIC,2,0,56,total,56,MANIFEST.size,off,len(body),off+m['resource_offset'],len(res),total,0,total,0]
    data=HEADER.pack(*fields)+encode_manifest(m)+body
    fields[-1]=crc(data); data=HEADER.pack(*fields)+data[56:]
    validate(data,profile)
    report={**m,'sdk_version':'0.0.2','package_size':len(data),'flash_body':len(body),
            'got':x['got_size'],'data':x['data_size'],'bss':x['bss_size'],'total_ram':x['ram_size'],
            'audited_relocations':x['audited_relocations'],'external_dependencies':[]}
    return data,report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True); p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--manifest',type=Path); p.add_argument('--out',type=Path,required=True)
    for key in ('app-id','name','version','author','description'): p.add_argument('--'+key,default='')
    p.add_argument('--permission',action='append',default=[]); p.add_argument('--resource',action='append',default=[])
    a=p.parse_args(); metadata=json.loads(a.manifest.read_text()) if a.manifest else {
        'app_id':a.app_id,'name':a.name,'version':a.version,'author':a.author,'description':a.description,'permissions':a.permission}
    resources=[]
    for raw in a.resource:
        name,path=raw.split('=',1) if '=' in raw else (Path(raw).name,raw)
        resources.append((name,Path(path)))
    data,report=pack(a.elf,load_profile(a.profile),metadata,resources)
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_bytes(data)
    a.out.with_suffix(a.out.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f"{a.out}: Flash {report['flash_body']} / GOT {report['got']} / data {report['data']} / BSS {report['bss']} / RAM {report['total_ram']}")
    return 0
if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,KeyError,OSError) as e: raise SystemExit(str(e))
