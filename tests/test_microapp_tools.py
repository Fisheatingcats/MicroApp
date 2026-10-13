"""Wire rejection tests plus real ARM ELF audits when the toolchain is present."""
import copy
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
SDK=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(SDK/'tools/microapp'))
from microapp_profile import load_profile
from microapp_wire import HEADER, MANIFEST, FIXUP, FIELDS, HFIELDS, crc, encode_manifest, validate
from pack_microapp import pack
from microapp_elf import extract_elf

PROFILE=load_profile(SDK/'examples/counter/target.json')
META=json.loads((SDK/'examples/counter/app.json').read_text())
SEGMENTS=dict(image=b'\x00\xbf\x70\x47', initial=bytes(24),fixups=[(12,1,1),(16,28,2)],
              entry_offset=1,image_alignment=4,ram_size=32,ram_alignment=8,got_offset=0,got_size=12,
              bss_offset=24,bss_size=8,data_size=12,audited_relocations=2)

def rebuild(data,m,body):
    m['image_crc32']=crc(body[:m['image_size']]); m['body_crc32']=crc(body)
    m['resource_crc32']=crc(body[m['resource_offset']:])
    h=list(HEADER.unpack_from(data)); h[-1]=0
    out=HEADER.pack(*h)+encode_manifest(m)+body
    h[-1]=crc(out); return HEADER.pack(*h)+out[56:]

class WireTests(unittest.TestCase):
    def setUp(self):
        with patch('pack_microapp.extract_elf',return_value=SEGMENTS):
            self.data,_=pack(Path('unused'),PROFILE,META)
        _,self.m,self.body=validate(self.data,PROFILE)
    def test_no_placement(self):
        self.assertNotIn('slot_base',self.m); self.assertNotIn('ram_base',self.m)
        self.assertEqual(self.m['entry_offset'],1)
    def test_legacy_and_crc(self):
        bad=bytearray(self.data); bad[4]=1
        with self.assertRaisesRegex(ValueError,'unsupported format'): validate(bad,PROFILE)
        for off in (80,364,380,len(self.data)-1):
            bad=bytearray(self.data); bad[off]^=1
            with self.assertRaises(ValueError): validate(bad,PROFILE)
    def test_abi_and_capacity(self):
        for field,value in [('float_abi',0),('abi_major',2),('architecture',9),('ram_size',8193),('permission_mask',128)]:
            m=copy.copy(self.m); m[field]=value
            with self.assertRaises(ValueError): validate(rebuild(self.data,m,self.body),PROFILE)
    def test_bad_fixups(self):
        for f in ((12,1,99),(32,1,1),(12,5,1),(12,33,2),(16,1,1),(13,1,1)):
            body=bytearray(self.body); FIXUP.pack_into(body,self.m['fixup_offset'],*f)
            with self.assertRaises(ValueError): validate(rebuild(self.data,copy.copy(self.m),body),PROFILE)
    def test_invalid_ranges(self):
        for field,value in [('init_offset',0xffffffff),('fixup_count',0xffffffff),('entry_offset',0),
                            ('got_offset',24),('ram_alignment',3),('resource_offset',0)]:
            m=copy.copy(self.m);m[field]=value
            with self.assertRaises(ValueError): validate(rebuild(self.data,m,self.body),PROFILE)
    def test_resources(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'message';path.write_bytes(b'hello')
            with patch('pack_microapp.extract_elf',return_value=SEGMENTS):
                data,_=pack(Path('unused'),PROFILE,META,[('message',path)])
                with self.assertRaises(ValueError): pack(Path('unused'),PROFILE,META,[('message',path),('message',path)])
            _,m,body=validate(data,PROFILE); body=bytearray(body)
            struct.pack_into('<I',body,m['resource_offset']+12+32,0xffffffff)
            with self.assertRaises(ValueError): validate(rebuild(data,m,body),PROFILE)

@unittest.skipUnless(shutil.which('arm-none-eabi-gcc') and shutil.which('cmake'),'ARM toolchain required')
class ElfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='microapp-sdk-')
        cls.path=Path(cls.temp.name)
        # An exported SDK snapshot is built with no parent checkout in includes.
        cls.sdk=cls.path/'MicroApp'
        for folder in ('include','src','cmake','tools','examples'):
            shutil.copytree(SDK/folder,cls.sdk/folder,ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copy2(SDK/'CMakeLists.txt',cls.sdk/'CMakeLists.txt')
        cls.build=cls.path/'build'
        subprocess.run(['cmake','-S',str(cls.sdk/'examples/counter'),'-B',str(cls.build),'-G','Ninja',
                        '-DCMAKE_TOOLCHAIN_FILE='+str(cls.sdk/'cmake/arm-none-eabi.cmake'),
                        '-DPython3_EXECUTABLE='+sys.executable],check=True,capture_output=True)
        subprocess.run(['cmake','--build',str(cls.build)],check=True,capture_output=True)
        cls.elf=cls.build/'counter.elf'
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def test_real_standalone_elf(self):
        x=extract_elf(self.elf,'hard'); self.assertGreaterEqual(len(x['fixups']),9)
        self.assertTrue(any(f[2]==1 and f[1]&1 for f in x['fixups']))
        self.assertTrue(any(f[2]==2 for f in x['fixups']))
        validate((self.build/'counter.mapp').read_bytes(),PROFILE)
        commands=(self.build/'build.ninja').read_text()
        self.assertNotIn(str(SDK).replace('\\','/'),commands)
        includes='\n'.join(line for line in commands.splitlines() if 'INCLUDES =' in line)
        for dependency in ('LVGL','FreeWatch','HAL','FreeRTOS'):
            self.assertTrue(dependency not in includes, f'Unexpected include dependency: {dependency}')
    def test_wrong_float_abi(self):
        with self.assertRaisesRegex(ValueError,'floating ABI'): extract_elf(self.elf,'soft')
    def test_unknown_relocation(self):
        from elftools.elf.elffile import ELFFile
        with self.elf.open('rb') as stream:
            e=ELFFile(stream); section=e.get_section_by_name('.rel.dyn'); pos=section['sh_offset']+4
        data=bytearray(self.elf.read_bytes());struct.pack_into('<I',data,pos,255)
        bad=self.path/'unknown.elf';bad.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'unsupported dynamic relocation'): extract_elf(bad,'hard')
    def test_external_absolute_pointer(self):
        from elftools.elf.elffile import ELFFile
        with self.elf.open('rb') as stream:
            e=ELFFile(stream); reloc=next(e.get_section_by_name('.rel.dyn').iter_relocations())
            addr=reloc['r_offset']; target=next(s for s in e.iter_sections() if s['sh_addr']<=addr<s['sh_addr']+s['sh_size'])
            pos=target['sh_offset']+addr-target['sh_addr']
        data=bytearray(self.elf.read_bytes());struct.pack_into('<I',data,pos,0x40000000)
        bad=self.path/'absolute.elf';bad.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'external absolute'): extract_elf(bad,'hard')
    def test_flash_relocation_rejected(self):
        from elftools.elf.elffile import ELFFile
        with self.elf.open('rb') as stream:
            e=ELFFile(stream); pos=e.get_section_by_name('.rel.dyn')['sh_offset']
        data=bytearray(self.elf.read_bytes());struct.pack_into('<I',data,pos,0x10000000)
        bad=self.path/'flash.elf';bad.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'modify Flash'): extract_elf(bad,'hard')
    def test_strict_undefined_link(self):
        source=self.path/'undefined.c';source.write_text('extern void missing(void); int microapp_main(void){missing();return 0;}')
        cmd=['arm-none-eabi-gcc','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard',
             '-fPIC','-msingle-pic-base','-mpic-register=r9','-mno-pic-data-is-text-relative',
             '-nostdlib','-shared','-Wl,-Bsymbolic,--no-undefined,--emit-relocs',
             '-T'+str(self.sdk/'cmake/microapp_pic.ld'),str(source),'-o',str(self.path/'undefined.elf')]
        result=subprocess.run(cmd,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0); self.assertIn('missing',result.stderr)
    def test_alignment_from_real_elf(self):
        source=self.path/'aligned.c'
        source.write_text('#include <stdint.h>\n'
            'volatile uint32_t aligned_data __attribute__((aligned(32)))=7;\n'
            'const char aligned_text[] __attribute__((aligned(32)))="abc";\n'
            'int32_t microapp_main(const void *api,void *ctx,uint32_t reason,void *arg){\n'
            '(void)api;(void)ctx;(void)reason;(void)arg;\n'
            'const volatile char *p=aligned_text; return (int32_t)(aligned_data+*p);}\n')
        elf=self.path/'aligned.elf'
        cmd=['arm-none-eabi-gcc','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard',
             '-fPIC','-msingle-pic-base','-mpic-register=r9','-mno-pic-data-is-text-relative',
             '-Os','-nostdlib','-shared','-Wl,-Bsymbolic,--no-undefined,--emit-relocs',
             '-T'+str(self.sdk/'cmake/microapp_pic.ld'),str(source),'-o',str(elf)]
        subprocess.run(cmd,check=True,capture_output=True)
        x=extract_elf(elf,'hard');self.assertGreaterEqual(x['ram_alignment'],32)
        self.assertGreaterEqual(x['image_alignment'],32)
        data,_=pack(elf,PROFILE,META);validate(data,PROFILE)
    def test_one_past_pointers(self):
        source=self.path/'one_past.c'
        source.write_text('#include <stdint.h>\n'
            'uint32_t array[2] __attribute__((aligned(8)));\n'
            'uint32_t *volatile array_end=array+2;\n'
            'const char text[4]="abc"; const char *volatile text_end=text+4;\n'
            'int32_t microapp_main(const void *a,void *b,uint32_t c,void *d){\n'
            '(void)a;(void)b;(void)c;(void)d; return (int32_t)((array_end-array)+(text_end-text));}\n')
        elf=self.path/'one_past.elf'
        subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard',
            '-fPIC','-msingle-pic-base','-mpic-register=r9','-mno-pic-data-is-text-relative','-Os',
            '-nostdlib','-shared','-Wl,-Bsymbolic,--no-undefined,--emit-relocs',
            '-T'+str(self.sdk/'cmake/microapp_pic.ld'),str(source),'-o',str(elf)],check=True,capture_output=True)
        x=extract_elf(elf,'hard')
        self.assertTrue(any(f[2]==2 and f[1]==x['ram_size'] for f in x['fixups']))
        self.assertTrue(any(f[2]==1 and f[1]==len(x['image']) for f in x['fixups']))
        data,_=pack(elf,PROFILE,META);validate(data,PROFILE)
if __name__=='__main__': unittest.main()
