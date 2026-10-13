/** NOR-like storage and interrupted-operation tests; no GUI/platform dependency. */
#include "microapp/microapp_store.h"
#include "microapp/microapp_crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { uint8_t *bytes; uint32_t size; } memory_t;
static uint8_t flash_bytes[4096], backup_bytes[8192];
static memory_t flash_memory = {flash_bytes, sizeof(flash_bytes)}, backup_memory = {backup_bytes, sizeof(backup_bytes)};
static int fail_after = -1, partial = 0, mutations = 0;
static int read_memory(void *user, uint32_t off, void *out, uint32_t size)
{
    memory_t *memory = user;
    if (off > memory->size || size > memory->size - off) return -1;
    memcpy(out, memory->bytes + off, size);
    return 0;
}
static int mutation_fails(void)
{
    ++mutations;
    if (fail_after < 0) return 0;
    if (fail_after-- == 0) { fail_after = -1; return 1; }
    return 0;
}
static int write_memory(void *user, uint32_t off, const void *data, uint32_t size)
{
    memory_t *memory = user;
    const uint8_t *src = data;
    if (off > memory->size || size > memory->size - off) return -1;
    int fails = mutation_fails();
    uint32_t actual = fails ? partial ? size / 2u : 0u : size;
    for (uint32_t i = 0; i < actual; ++i) {
        CHECK((memory->bytes[off+i] & src[i]) == src[i]);
        memory->bytes[off+i] &= src[i];
    }
    return fails ? -1 : 0;
}
static int erase_memory(void *user, uint32_t off, uint32_t size)
{
    memory_t *memory = user;
    CHECK(off == 0u && size == memory->size);
    int fails = mutation_fails();
    memset(memory->bytes, 0xff, fails ? partial ? size / 2u : 0u : size);
    return fails ? -1 : 0;
}
static microapp_storage_t flash = {read_memory, write_memory, erase_memory, &flash_memory, sizeof(flash_bytes)};
static microapp_storage_t backup = {read_memory, write_memory, erase_memory, &backup_memory, sizeof(backup_bytes)};
static const microapp_target_t target = {"test",1,2,1,0,1,0,4096,128};
static uint8_t body_bytes[52];
static memory_t body_memory = {body_bytes, sizeof(body_bytes)};
static microapp_reader_t body = {read_memory, &body_memory, sizeof(body_bytes)};
static microapp_manifest_t manifest(const char *id, unsigned value)
{
    microapp_manifest_t m = {0};
    memcpy(m.board_id,"test",5);
    snprintf(m.app_id,sizeof(m.app_id),"%s",id);
    memcpy(m.name,"App",4); memcpy(m.version,"2",2);
    m.architecture=1; m.float_abi=2; m.abi_major=1; m.min_host_version=1;
    m.image_size=4; m.image_alignment=32; m.entry_offset=1;
    m.ram_size=32; m.ram_alignment=8; m.got_size=12;
    m.init_offset=4; m.init_size=24; m.bss_offset=24; m.bss_size=8; m.data_size=12;
    m.fixup_offset=28; m.fixup_count=2; m.resource_offset=52; m.body_size=52;
    memset(body_bytes,0,sizeof(body_bytes));
    microapp_fixup_t fixups[2]={{12,1,1},{16,28,2}};
    memcpy(body_bytes+24,&value,4); memcpy(body_bytes+28,fixups,sizeof(fixups));
    m.image_crc32=microapp_crc32(body_bytes,m.image_size);
    m.body_crc32=microapp_crc32(body_bytes,m.body_size);
    return m;
}
static void mount(microapp_store_t *store)
{ CHECK(microapp_store_open(store,&flash,&backup,0x08060000,&target)==0); }
static void reset(microapp_store_t *store)
{
    fail_after=-1; partial=0; mutations=0;
    memset(flash_bytes,0xff,sizeof(flash_bytes)); memset(backup_bytes,0xff,sizeof(backup_bytes));
    mount(store);
}
static void install(microapp_store_t *store, const char *id, unsigned value)
{
    microapp_manifest_t m=manifest(id,value);
    CHECK(microapp_store_install(store,&m,&body,512u)==0);
}
static unsigned value_of(microapp_store_t *store, const char *id)
{
    microapp_store_entry_t entry;
    CHECK(microapp_store_find(store,id,&entry)==0);
    CHECK(((0x08060000 + entry.body_offset) & 31u)==0u);
    unsigned value; memcpy(&value,flash_bytes+entry.body_offset+24,4);
    return value;
}
static int count_visit(void *user, const microapp_store_entry_t *entry)
{ CHECK(entry->manifest.app_id[0]); ++*(unsigned *)user; return 0; }
static void test_install_update_remove(void)
{
    microapp_store_t store; reset(&store);
    install(&store,"a",7); install(&store,"b",8);
    uint8_t saved[52]; microapp_store_entry_t b;
    CHECK(microapp_store_find(&store,"b",&b)==0); memcpy(saved,flash_bytes+b.body_offset,52);
    install(&store,"a",9);
    CHECK(value_of(&store,"a")==9 && value_of(&store,"b")==8);
    CHECK(microapp_store_remove(&store,"a")==0);
    microapp_store_entry_t entry;
    CHECK(microapp_store_find(&store,"a",&entry)==MICROAPP_STORE_NOT_FOUND); /* No older-version resurrection. */
    unsigned count=0; CHECK(microapp_store_foreach(&store,count_visit,&count)==0 && count==1);
    CHECK(microapp_store_compact(&store)==0);
    mount(&store);
    CHECK(value_of(&store,"b")==8 && microapp_store_find(&store,"a",&entry)==MICROAPP_STORE_NOT_FOUND);
    CHECK(microapp_store_find(&store,"b",&b)==0 && memcmp(saved,flash_bytes+b.body_offset,52)==0);
    microapp_store_usage_t usage;
    CHECK(microapp_store_usage(&store,&usage)==0 && usage.count==1 && usage.used_bytes==576);
    /* Deleted space is reused by automatic compaction. */
    for(unsigned i=0;i<20;++i) install(&store,"b",i);
    CHECK(value_of(&store,"b")==19);
    install(&store,"c",10);
    CHECK(value_of(&store,"b")==19 && value_of(&store,"c")==10);
    /* Invalid bodies and capacity exhaustion must preserve installed Apps. */
    microapp_manifest_t m=manifest("bad",4); body_bytes[0]^=1;
    uint8_t snapshot[4096]; memcpy(snapshot,flash_bytes,sizeof(snapshot));
    CHECK(microapp_store_install(&store,&m,&body,512)!=0 && memcmp(snapshot,flash_bytes,sizeof(snapshot))==0);
    m=manifest("full",4);
    CHECK(microapp_store_install(&store,&m,&body,4096)==MICROAPP_STORE_FULL && memcmp(snapshot,flash_bytes,sizeof(snapshot))==0);
}
static void test_append_interruptions(void)
{
    for(int mode=0;mode<2;++mode) for(int cut=0;cut<3;++cut) {
        microapp_store_t store; reset(&store);
        install(&store,"a",7); install(&store,"b",8);
        microapp_manifest_t m=manifest("a",9);
        fail_after=cut; partial=mode;
        CHECK(microapp_store_install(&store,&m,&body,512)!=0);
        fail_after=-1; mount(&store);
        CHECK(value_of(&store,"a")==7 && value_of(&store,"b")==8);
        install(&store,"c",10); /* Partial header/body cannot overwrite existing data. */
        CHECK(value_of(&store,"a")==7 && value_of(&store,"b")==8 && value_of(&store,"c")==10);
    }
    for(int mode=0;mode<2;++mode) {
        microapp_store_t store; reset(&store);
        install(&store,"a",7); install(&store,"b",8);
        partial=mode; fail_after=0;
        CHECK(microapp_store_remove(&store,"a")!=0);
        fail_after=-1; mount(&store);
        CHECK(value_of(&store,"b")==8);
        microapp_store_entry_t entry;
        CHECK(microapp_store_find(&store,"a",&entry)==(mode ? MICROAPP_STORE_NOT_FOUND : 0));
    }
}
static void test_compaction_interruptions(void)
{
    microapp_store_t store; reset(&store);
    install(&store,"a",7); install(&store,"b",8); install(&store,"c",9); CHECK(microapp_store_remove(&store,"a")==0);
    mutations=0; CHECK(microapp_store_compact(&store)==0); int operations=mutations;
    CHECK(operations>8);
    for(int mode=0;mode<2;++mode) for(int cut=0;cut<operations;++cut) {
        reset(&store); install(&store,"a",7); install(&store,"b",8); install(&store,"c",9); CHECK(microapp_store_remove(&store,"a")==0);
        fail_after=cut; partial=mode;
        CHECK(microapp_store_compact(&store)!=0);
        fail_after=-1; mount(&store);
        CHECK(value_of(&store,"b")==8 && value_of(&store,"c")==9);
        microapp_store_entry_t entry; CHECK(microapp_store_find(&store,"a",&entry)==MICROAPP_STORE_NOT_FOUND);
    }
    printf("PASS: %d compaction interruption points, before/partial mutations\n",operations);
}
static void test_legacy_migration(void)
{
    microapp_store_t store; reset(&store);
    microapp_slot_header_t legacy={0};
    legacy.magic=MICROAPP_SLOT_MAGIC; legacy.format_major=2;
    legacy.header_size=sizeof(legacy); legacy.manifest_size=sizeof(legacy.manifest);
    legacy.image_offset=512; legacy.body_size=52; legacy.manifest=manifest("legacy",7);
    legacy.header_crc32=microapp_crc32((const uint8_t *)&legacy,sizeof(legacy));
    memcpy(flash_bytes,&legacy,sizeof(legacy)); memcpy(flash_bytes+512,body_bytes,52);
    CHECK(value_of(&store,"legacy")==7);
    install(&store,"new",8);
    CHECK(value_of(&store,"legacy")==7 && value_of(&store,"new")==8);
    CHECK(microapp_store_remove(&store,"legacy")==0);
    mount(&store); CHECK(value_of(&store,"new")==8);
}
static void test_committed_corruption(void)
{
    for(unsigned fault=0;fault<4;++fault) {
        microapp_store_t store; reset(&store); install(&store,"a",7);
        microapp_store_record_t record; memcpy(&record,flash_bytes,sizeof(record));
        if (fault==0) record.manifest.abi_major=999;
        if (fault==1) record.span=UINT32_MAX;
        if (fault==2) record.body_offset=record.span;
        if (fault==3) record.version=2;
        record.crc32=0; record.commit=record.deleted=UINT32_MAX;
        record.crc32=microapp_crc32((const uint8_t *)&record,sizeof(record)); record.commit=0;
        memcpy(flash_bytes,&record,sizeof(record));
        uint8_t snapshot[4096]; memcpy(snapshot,flash_bytes,sizeof(snapshot));
        microapp_store_usage_t usage;
        CHECK(microapp_store_usage(&store,&usage)==MICROAPP_STORE_CORRUPT);
        microapp_manifest_t m=manifest("b",8);
        CHECK(microapp_store_install(&store,&m,&body,512)==MICROAPP_STORE_CORRUPT);
        CHECK(memcmp(snapshot,flash_bytes,sizeof(snapshot))==0);
    }
}
int main(void)
{
    test_install_update_remove(); test_append_interruptions();
    test_compaction_interruptions(); test_legacy_migration(); test_committed_corruption();
    puts("PASS: multiple Apps, latest update, tombstones, GC, legacy migration, recovery and immutable bodies");
    return 0;
}
