/**
 * @brief Streaming layout/CRC audits and RAM-only pointer preparation.
 * @file microapp_native.c
 */
#include "microapp/microapp_native.h"
#include "microapp/microapp_crc32.h"
#include <string.h>

/**
 * @brief Check a byte range with subtraction to avoid unsigned addition overflow.
 * @param off Reader-relative start byte.
 * @param len Range byte count.
 * @param total Available bytes.
 * @return Nonzero if off/len lie within total.
 */
static int range(uint32_t off, uint32_t len, uint32_t total)
{ return off <= total && len <= total - off; }
/**
 * @brief Bound an exact read and translate the callback failure.
 * @param r Bounded source reader.
 * @param off Reader-relative start byte.
 * @param dst Writable destination buffer.
 * @param len Range byte count.
 * @return OK, INVALID for bounds/reader, or IO.
 */
static int read_at(const microapp_reader_t *r, uint32_t off, void *dst, uint32_t len)
{
    if (!r || !r->read || !range(off,len,r->size)) return MICROAPP_NATIVE_INVALID;
    return r->read(r->user,off,dst,len)==0 ? 0 : MICROAPP_NATIVE_IO;
}
/**
 * @brief Stream CRC in 128-byte chunks, optionally normalizing the package CRC field.
 * @param r Bounded source reader.
 * @param off Reader-relative start byte.
 * @param len Range byte count.
 * @param expected Expected CRC32.
 * @param package Nonzero to normalize package CRC bytes.
 * @return OK, INVALID for CRC mismatch, or IO.
 * @details Package mode treats absolute bytes 52..55 as zero.
 */
static int crc_at(const microapp_reader_t *r, uint32_t off, uint32_t len, uint32_t expected,
                  int package)
{
    uint8_t buf[128]; uint32_t crc=0u, done=0;
    while (done<len) {
        uint32_t n=len-done; if(n>sizeof(buf)) n=sizeof(buf);
        if(read_at(r,off+done,buf,n)) return MICROAPP_NATIVE_IO;
        if(package) for(uint32_t i=0;i<n;i++)
            if(off+done+i>=52u && off+done+i<56u) buf[i]=0;
        crc=microapp_crc32_update(crc,buf,n); done+=n;
    }
    return crc==expected ? 0 : MICROAPP_NATIVE_INVALID;
}
/**
 * @brief Require a bounded NUL terminator and optional nonempty text.
 * @param s Bounded text.
 * @param n Exact range length.
 * @param nonempty Nonzero to require nonempty text.
 * @return Nonzero when the text constraint holds.
 */
static int text_valid(const char *s, size_t n, int nonempty)
{ return (!nonempty || s[0]) && memchr(s,0,n)!=NULL; }
/** @brief Validate metadata text, ABI policy and all body/RAM layout bounds.
 * @see microapp_validate_manifest
 */
int microapp_validate_manifest(const microapp_manifest_t *m, const microapp_target_t *t)
{
    if(!m || !text_valid(m->board_id,32,1) || !text_valid(m->app_id,32,1) ||
       !text_valid(m->name,32,1) || !text_valid(m->version,16,1) ||
       !text_valid(m->author,32,0) || !text_valid(m->description,64,0)) return -1;
    if(m->architecture!=MICROAPP_ARCH_ARMV7EM || m->float_abi>MICROAPP_FLOAT_HARD) return -2;
    if(t && (!t->board_id || strcmp(m->board_id,t->board_id) || m->architecture!=t->architecture ||
       m->float_abi!=t->float_abi || m->abi_major!=t->abi_major || m->abi_minor>t->abi_minor ||
       m->min_host_version>t->host_version || (m->permission_mask & ~t->permissions) ||
       m->body_size>t->max_body_size || m->ram_size>t->max_ram_size)) return -2;
    if(!m->image_size || !(m->entry_offset&1u) || (m->entry_offset&~1u)>=m->image_size ||
       m->image_alignment<4u || (m->image_alignment&(m->image_alignment-1u)) ||
       m->image_alignment>4096u ||
       !m->ram_size || m->ram_alignment<4u || (m->ram_alignment&(m->ram_alignment-1u)) ||
       m->ram_alignment>4096u || m->init_size>m->ram_size || m->data_size>m->init_size ||
       m->bss_offset<m->init_size || !range(m->bss_offset,m->bss_size,m->ram_size) ||
       m->bss_offset+m->bss_size!=m->ram_size || (m->got_offset&3u) || (m->got_size&3u) ||
       !range(m->got_offset,m->got_size,m->init_size) || m->got_size<12u ||
       m->init_offset<m->image_size || (m->init_offset&3u) ||
       !range(m->init_offset,m->init_size,m->body_size) ||
       m->fixup_offset<m->init_offset+m->init_size || (m->fixup_offset&3u) ||
       m->fixup_count>m->init_size/4u || m->fixup_count>UINT32_MAX/12u ||
       !range(m->fixup_offset,m->fixup_count*12u,m->body_size) ||
       m->resource_offset<m->fixup_offset+m->fixup_count*12u ||
       !range(m->resource_offset,m->resource_size,m->body_size) ||
       m->resource_offset+m->resource_size!=m->body_size) return -1;
    return 0;
}
/** @brief Verify immutable body CRCs, ordered RAM fixups and resource table bounds.
 * @see microapp_validate_body
 */
int microapp_validate_body(const microapp_manifest_t *m, const microapp_reader_t *r)
{
    microapp_fixup_t f; uint32_t last=0;
    int rc=microapp_validate_manifest(m,NULL); if(rc) return rc;
    if(!r || r->size!=m->body_size) return -1;
    if(crc_at(r,0,m->body_size,m->body_crc32,0) ||
       crc_at(r,0,m->image_size,m->image_crc32,0) ||
       crc_at(r,m->resource_offset,m->resource_size,m->resource_crc32,0)) return -1;
    for(uint32_t i=0;i<m->fixup_count;i++) {
        if(read_at(r,m->fixup_offset+i*12u,&f,sizeof(f))) return -3;
        if((f.ram_offset&3u) || !range(f.ram_offset,4u,m->init_size) ||
           (i && f.ram_offset<=last) ||
           (f.kind==MICROAPP_FIXUP_FLASH ? f.target_offset>m->image_size :
            f.kind==MICROAPP_FIXUP_RAM ? f.target_offset>m->ram_size : 1)) return -1;
        last=f.ram_offset;
    }
    if(m->resource_size) {
        microapp_resource_table_header_t h; microapp_resource_entry_t e;
        if(read_at(r,m->resource_offset,&h,sizeof(h))) return -3;
        if(h.magic!=MICROAPP_RESOURCE_TABLE_MAGIC || h.version!=1u ||
           h.table_size!=12u+44u*h.entry_count || h.table_size>m->resource_size) return -1;
        uint32_t end=h.table_size;
        for(uint32_t i=0;i<h.entry_count;i++) {
            if(read_at(r,m->resource_offset+12u+44u*i,&e,sizeof(e))) return -3;
            if(!text_valid(e.name,32,1) || e.offset<end ||
               !range(e.offset,e.size,m->resource_size)) return -1;
            if(crc_at(r,m->resource_offset+e.offset,e.size,e.crc32,0)) return -1;
            end=e.offset+e.size;
        }
    }
    return 0;
}
/** @brief Borrowed package reader with body-relative origin. */
typedef struct {
    const microapp_reader_t *parent; /**< Whole-package reader. */
    uint32_t base; /**< Body start in the parent. */
} sub_reader_t;
/**
 * @brief Forward a body-relative read to its parent package reader.
 * @param user Borrowed adapter/visitor context.
 * @param off Reader-relative start byte.
 * @param dst Writable destination buffer.
 * @param n Exact range length.
 * @return Parent read result.
 * @details Caller construction ensures the base plus body offset is within the package.
 */
static int sub_read(void *user,uint32_t off,void *dst,uint32_t n)
{ sub_reader_t *s=user; return read_at(s->parent,s->base+off,dst,n); }
/** @brief Validate complete package layout, metadata and body without writing storage.
 * @see microapp_validate_package
 */
int microapp_validate_package(const microapp_reader_t *r,const microapp_target_t *t,
                             microapp_package_header_t *h,microapp_manifest_t *m)
{
    if(!h || !m || read_at(r,0,h,sizeof(*h))) return -1;
    if(h->magic!=MICROAPP_PACKAGE_MAGIC || h->format_major!=2u || h->format_minor!=0u) return -2;
    if(h->header_size!=56u || h->total_size!=r->size || h->manifest_offset!=56u ||
       h->manifest_size!=sizeof(*m) || h->image_offset!=56u+sizeof(*m) ||
       !range(h->image_offset,h->image_size,h->signed_size) ||
       h->signed_size!=h->image_offset+h->image_size || h->signature_offset!=h->signed_size ||
       !range(h->signature_offset,h->signature_size,r->size) ||
       h->signature_offset+h->signature_size!=r->size) return -1;
    if(crc_at(r,0,h->signed_size,h->crc32,1)) return -1;
    if(read_at(r,56u,m,sizeof(*m))) return -3;
    int rc=microapp_validate_manifest(m,t); if(rc) return rc;
    if(m->body_size!=h->image_size || h->resource_offset!=h->image_offset+m->resource_offset ||
       h->resource_size!=m->resource_size) return -1;
    sub_reader_t sub={r,h->image_offset};
    microapp_reader_t body={sub_read,&sub,m->body_size};
    return microapp_validate_body(m,&body);
}
/** @brief Validate, zero instance RAM, copy initialization bytes and apply RAM-only fixups.
 * @see microapp_prepare_ram
 */
int microapp_prepare_ram(const microapp_manifest_t *m,const microapp_reader_t *r,
                        void *ram,uint32_t capacity,uint32_t flash,uint32_t address)
{
    int rc=microapp_validate_body(m,r); if(rc) return rc;
    if(!ram || capacity<m->ram_size || (address&(m->ram_alignment-1u)) ||
       ((uintptr_t)ram&(m->ram_alignment-1u)) || (flash&(m->image_alignment-1u)) ||
       m->image_size>UINT32_MAX-flash || m->ram_size>UINT32_MAX-address) return -1;
    memset(ram,0,m->ram_size);
    if(read_at(r,m->init_offset,ram,m->init_size)) return -3;
    for(uint32_t i=0;i<m->fixup_count;i++) {
        microapp_fixup_t f;
        if(read_at(r,m->fixup_offset+i*12u,&f,sizeof(f))) return -3;
        uint32_t value=(f.kind==MICROAPP_FIXUP_FLASH ? flash : address)+f.target_offset;
        memcpy((uint8_t*)ram+f.ram_offset,&value,4u);
    }
    return 0;
}
