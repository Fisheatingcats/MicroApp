#include "microapp/microapp_native.h"
#include "microapp/microapp_crc32.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int read_memory(void *user,uint32_t off,void *dst,uint32_t n)
{ memcpy(dst,(const uint8_t*)user+off,n); return 0; }
static void seal(microapp_manifest_t *m,const uint8_t *body)
{ m->image_crc32=microapp_crc32(body,m->image_size); m->body_crc32=microapp_crc32(body,m->body_size); }
int main(void)
{
    uint8_t body[52]={0};
    microapp_manifest_t m={0};
    memcpy(m.board_id,"test",5); memcpy(m.app_id,"app",4); memcpy(m.name,"App",4); memcpy(m.version,"2",2);
    m.architecture=1; m.float_abi=2; m.abi_major=1; m.min_host_version=1;
    m.image_size=4; m.image_alignment=4; m.entry_offset=1; m.ram_size=32; m.ram_alignment=8; m.got_size=12;
    m.init_offset=4; m.init_size=24; m.bss_offset=24; m.bss_size=8; m.data_size=12;
    m.fixup_offset=28; m.fixup_count=2; m.resource_offset=52; m.body_size=52;
    microapp_fixup_t fixups[2]={{12,1,1},{16,28,2}};
    uint32_t count=7;
    memcpy(body+4+20,&count,4); memcpy(body+28,fixups,sizeof(fixups)); seal(&m,body);
    microapp_reader_t reader={read_memory,body,sizeof(body)};
    microapp_target_t target={"test",1,2,1,0,1,0,4096,128};
    assert(microapp_validate_manifest(&m,&target)==0);
    _Alignas(32) uint8_t ram[40];
    const uint32_t flash[]={0x08060200,0x08061200};
    const uint32_t address[]={0x2001e000,0x2001e100};
    for(unsigned i=0;i<2;i++) for(unsigned j=0;j<2;j++) {
        memset(ram,0xaa,sizeof(ram));
        assert(microapp_prepare_ram(&m,&reader,ram,sizeof(ram),flash[i],address[j])==0);
        uint32_t a,b,c; memcpy(&a,ram+12,4);memcpy(&b,ram+16,4);memcpy(&c,ram+20,4);
        assert(a==flash[i]+1 && b==address[j]+28 && c==7);
        for(unsigned k=24;k<32;k++) assert(ram[k]==0);
        for(unsigned k=32;k<40;k++) assert(ram[k]==0xaa);
    }
    memset(ram,0xaa,sizeof(ram));
    assert(microapp_prepare_ram(&m,&reader,ram,31,flash[0],address[0])!=0);
    assert(microapp_prepare_ram(&m,&reader,ram,40,flash[0],address[0]+4)!=0);
    for(unsigned k=0;k<40;k++) assert(ram[k]==0xaa);
    microapp_manifest_t align_m=m;
    align_m.ram_alignment=32;
    assert(microapp_prepare_ram(&align_m,&reader,ram,40,flash[0],address[0]+8)!=0);
    align_m=m; align_m.image_alignment=32;
    assert(microapp_prepare_ram(&align_m,&reader,ram,40,flash[0]+4,address[0])!=0);
    for(unsigned k=0;k<40;k++) assert(ram[k]==0xaa);
    microapp_manifest_t bad=m; bad.float_abi=0; assert(microapp_validate_manifest(&bad,&target)!=0);
    bad=m; bad.init_offset=UINT32_MAX; assert(microapp_validate_body(&bad,&reader)!=0);
    bad=m; bad.fixup_count=UINT32_MAX; assert(microapp_validate_body(&bad,&reader)!=0);
    microapp_fixup_t ends[2]={{12,4,1},{16,32,2}};
    memcpy(body+28,ends,sizeof(ends)); seal(&m,body);
    assert(microapp_prepare_ram(&m,&reader,ram,40,flash[0],address[0])==0);
    uint32_t end_flash,end_ram;
    memcpy(&end_flash,ram+12,4); memcpy(&end_ram,ram+16,4);
    assert(end_flash==flash[0]+4 && end_ram==address[0]+32);
    memcpy(body+28,fixups,sizeof(fixups)); seal(&m,body);
    memset(ram,0xaa,sizeof(ram));
    for(unsigned n=0;n<4;n++) {
        microapp_fixup_t f=fixups[0];
        if(n==0) f.kind=99;
        if(n==1) f.ram_offset=32;
        if(n==2) f.target_offset=5;
        if(n==3) f.ram_offset=16;
        memcpy(body+28,&f,12); seal(&m,body);
        assert(microapp_validate_body(&m,&reader)!=0);
        assert(microapp_prepare_ram(&m,&reader,ram,40,flash[0],address[0])!=0);
        for(unsigned k=0;k<40;k++) assert(ram[k]==0xaa);
    }
    memcpy(body+28,fixups,sizeof(fixups)); seal(&m,body);
    uint8_t package[368+52]={0};
    microapp_package_header_t h={MICROAPP_PACKAGE_MAGIC,2,0,56,sizeof(package),56,sizeof(m),368,52,420,0,420,0,420,0};
    memcpy(package,&h,sizeof(h)); memcpy(package+56,&m,sizeof(m)); memcpy(package+368,body,sizeof(body));
    h.crc32=microapp_crc32(package,sizeof(package)); memcpy(package,&h,sizeof(h));
    microapp_reader_t p={read_memory,package,sizeof(package)};
    microapp_package_header_t parsed; microapp_manifest_t manifest;
    assert(microapp_validate_package(&p,&target,&parsed,&manifest)==0);
    package[4]=1; assert(microapp_validate_package(&p,&target,&parsed,&manifest)==MICROAPP_NATIVE_UNSUPPORTED);
    package[4]=2; package[200]^=1; assert(microapp_validate_package(&p,&target,&parsed,&manifest)!=0);
    puts("PASS: package validation, four RAM preparations, Thumb/data/BSS, malformed records and bounds");
    return 0;
}
