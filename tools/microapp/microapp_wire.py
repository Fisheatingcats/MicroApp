"""MicroApp 2.0 wire encoding and strict package validation."""
import binascii
import struct
HEADER = struct.Struct('<IHH12I')
MANIFEST = struct.Struct('<32s32s32s16s32s64s26I')
FIXUP = struct.Struct('<3I')
RESOURCE_HEADER = struct.Struct('<IHHI')
RESOURCE_ENTRY = struct.Struct('<32s3I')
MAGIC = 0x5050414d
RESOURCE_MAGIC = 0x5452414d
STRINGS = ('board_id','app_id','name','version','author','description')
FIELDS = ('abi_major','abi_minor','min_host_version','permission_mask','architecture','float_abi',
          'image_size','image_alignment','image_crc32','entry_offset','ram_size','ram_alignment','got_offset','got_size',
          'init_offset','init_size','bss_offset','bss_size','data_size','fixup_offset','fixup_count',
          'resource_offset','resource_size','resource_crc32','body_size','body_crc32')
HFIELDS = ('magic','major','minor','header_size','total_size','manifest_offset','manifest_size',
           'image_offset','image_size','resource_offset','resource_size','signature_offset',
           'signature_size','signed_size','crc32')
def crc(data): return binascii.crc32(data) & 0xffffffff
def align(data): return data + bytes((-len(data)) % 4)
def cstr(s,n):
    raw = s.encode('ascii')
    if b'\0' in raw or len(raw) >= n: raise ValueError('invalid metadata string')
    return raw + bytes(n-len(raw))
def decode(raw,required=False):
    if b'\0' not in raw or (required and not raw[0]): raise ValueError('invalid metadata string')
    return raw.split(b'\0',1)[0].decode('ascii')
def span(off,size,total): return 0 <= off <= total and 0 <= size <= total-off
def require(condition,message):
    if not condition: raise ValueError(message)
def resource_blob(resources):
    names = set(); entries = bytearray(); payload = bytearray()
    require(len(resources)<=65535,'too many resources')
    total = 12+44*len(resources)
    for name,path in resources:
        require(name and name not in names,'empty or duplicate resource name')
        names.add(name); raw=path.read_bytes()
        entries.extend(RESOURCE_ENTRY.pack(cstr(name,32),total+len(payload),len(raw),crc(raw)))
        payload.extend(align(raw))
    return RESOURCE_HEADER.pack(RESOURCE_MAGIC,1,len(resources),total)+entries+payload if resources else b''
def encode_manifest(m):
    return MANIFEST.pack(*(cstr(m[k],n) for k,n in zip(STRINGS,(32,32,32,16,32,64))),*(m[k] for k in FIELDS))
def validate(data,profile=None):
    require(len(data)>=HEADER.size,'short package')
    h=dict(zip(HFIELDS,HEADER.unpack_from(data)))
    require((h['magic'],h['major'],h['minor'])==(MAGIC,2,0),'unsupported format (requires 2.0)')
    require(h['header_size']==56 and h['total_size']==len(data) and h['manifest_offset']==56 and
            h['manifest_size']==MANIFEST.size and h['image_offset']==56+MANIFEST.size and
            h['signed_size']==h['image_offset']+h['image_size'] and h['signature_offset']==h['signed_size'] and
            h['total_size']==h['signed_size']+h['signature_size'],'invalid package layout')
    require(h['signed_size']<=len(data),'invalid signed range')
    signed=bytearray(data[:h['signed_size']]); signed[52:56]=bytes(4)
    require(crc(signed)==h['crc32'],'package CRC mismatch')
    require(len(data)>=56+MANIFEST.size,'short manifest')
    values=MANIFEST.unpack_from(data,56)
    m={k:decode(v,i<4) for i,(k,v) in enumerate(zip(STRINGS,values[:6]))}
    m.update(zip(FIELDS,values[6:]))
    require(m['architecture']==1 and m['float_abi'] in (0,1,2),'unsupported architecture or float ABI')
    if profile:
        require(m['board_id']==profile['board_id'] and m['abi_major']==profile['abi_major'] and
                m['abi_minor']<=profile['abi_minor'] and m['min_host_version']<=profile['host_version'] and
                m['float_abi']==('soft','softfp','hard').index(profile['float_abi']) and
                m['permission_mask'] & ~sum(profile.get('permissions',{}).values())==0 and
                m['body_size']<=profile['max_body_size'] and m['ram_size']<=profile['max_ram_size'],
                'incompatible host ABI/target/capacity/permission')
    require(m['body_size']==h['image_size'] and h['resource_offset']==h['image_offset']+m['resource_offset'] and
            h['resource_size']==m['resource_size'],'header/manifest mismatch')
    body=data[h['image_offset']:h['signed_size']]
    require(m['image_size']>0 and m['entry_offset'] & 1 and (m['entry_offset'] & ~1)<m['image_size'], 'invalid entry')
    a=m['image_alignment']
    require(4<=a<=4096 and a & (a-1)==0,'invalid image alignment')
    a=m['ram_alignment']
    require(4<=a<=4096 and a & (a-1)==0 and m['ram_size']>0 and m['init_size']<=m['ram_size'] and
            m['bss_offset']>=m['init_size'] and m['bss_offset']+m['bss_size']==m['ram_size'] and
            m['data_size']<=m['init_size'],'invalid RAM layout')
    require(m['got_offset']%4==0 and m['got_size']>=12 and m['got_size']%4==0 and
            span(m['got_offset'],m['got_size'],m['init_size']),'invalid GOT')
    require(m['init_offset']>=m['image_size'] and m['init_offset']%4==0 and
            span(m['init_offset'],m['init_size'],len(body)) and
            m['fixup_offset']>=m['init_offset']+m['init_size'] and m['fixup_offset']%4==0 and
            m['fixup_count']<=m['init_size']//4 and span(m['fixup_offset'],m['fixup_count']*12,len(body)) and
            m['resource_offset']>=m['fixup_offset']+m['fixup_count']*12 and
            m['resource_offset']+m['resource_size']==len(body),'invalid body layout')
    require(crc(body)==m['body_crc32'] and crc(body[:m['image_size']])==m['image_crc32'],'body/image CRC mismatch')
    last=-1
    for i in range(m['fixup_count']):
        off,target,kind=FIXUP.unpack_from(body,m['fixup_offset']+i*12)
        require(off>last and off%4==0 and span(off,4,m['init_size']) and
                ((kind==1 and target<=m['image_size']) or (kind==2 and target<=m['ram_size'])),
                'invalid RAM fixup')
        last=off
    res=body[m['resource_offset']:]
    require(crc(res)==m['resource_crc32'],'resource CRC mismatch')
    if res:
        require(len(res)>=12,'short resource table')
        magic,version,count,table_size=RESOURCE_HEADER.unpack_from(res)
        require(magic==RESOURCE_MAGIC and version==1 and table_size==12+44*count and table_size<=len(res),'bad resources')
        end=table_size; names=set()
        for i in range(count):
            name,off,size,check=RESOURCE_ENTRY.unpack_from(res,12+44*i); name=decode(name,True)
            require(name not in names and off>=end and span(off,size,len(res)) and crc(res[off:off+size])==check,'bad resource entry')
            names.add(name); end=off+size
    return h,m,body
