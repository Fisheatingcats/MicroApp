/**
 * @brief NOR append transactions, latest-version scan and backup-first compaction.
 * @file microapp_store.c
 */
#include "microapp/microapp_store.h"
#include "microapp/microapp_crc32.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(microapp_store_record_t) == 512u, "store header layout");
/** @brief Private durable backup journal, normalized CRC excludes commit/consumed. */
typedef struct {
    uint32_t magic; /**< Journal magic. */
    uint32_t version; /**< Journal version 1. */
    uint32_t size; /**< Primary region bytes backed up. */
    uint32_t crc32; /**< CRC of complete backup image. */
    uint32_t header_crc32; /**< Normalized journal header CRC. */
    uint32_t commit; /**< Zero makes the backup durable recovery authority. */
    uint32_t consumed; /**< Zero means primary restoration completed. */
} journal_t;
/** @brief Borrowed storage slice used by native body validation. */
typedef struct {
    microapp_storage_t *storage; /**< Storage callback source. */
    uint32_t offset; /**< Slice origin relative to storage. */
} slice_t;
/**
 * @brief Map bounded slice-relative reads to a storage region.
 * @param user Borrowed adapter/visitor context.
 * @param offset Region-relative byte offset.
 * @param dst Writable destination buffer.
 * @param size Range size in bytes.
 * @return Driver result, or nonzero when bounds fail.
 */
static int read_slice(void *user, uint32_t offset, void *dst, uint32_t size)
{
    slice_t *slice = user;
    if (slice->offset > slice->storage->size || offset > slice->storage->size - slice->offset ||
        size > slice->storage->size - slice->offset - offset) return -1;
    return slice->storage->read(slice->storage->user, slice->offset + offset, dst, size);
}
/**
 * @brief Compute a region CRC in 256-byte chunks.
 * @param region Borrowed storage callbacks.
 * @param offset Region-relative byte offset.
 * @param size Range size in bytes.
 * @param crc Output CRC accumulator.
 * @return OK or IO.
 */
static int region_crc(microapp_storage_t *region, uint32_t offset, uint32_t size, uint32_t *crc)
{
    uint8_t buf[256];
    *crc = 0u;
    while (size) {
        uint32_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (region->read(region->user, offset, buf, chunk)) return MICROAPP_STORE_IO;
        *crc = microapp_crc32_update(*crc, buf, chunk);
        offset += chunk;
        size -= chunk;
    }
    return 0;
}
/**
 * @brief Inspect whether the entire range is erased without allocating an image buffer.
 * @param region Borrowed storage callbacks.
 * @param offset Region-relative byte offset.
 * @param size Range size in bytes.
 * @return 1 for all FF, 0 for programmed bytes, or a negative store I/O result.
 */
static int blank(microapp_storage_t *region, uint32_t offset, uint32_t size)
{
    uint8_t buf[256];
    while (size) {
        uint32_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (region->read(region->user, offset, buf, chunk)) return MICROAPP_STORE_IO;
        for (uint32_t i = 0u; i < chunk; ++i) if (buf[i] != 0xffu) return 0;
        offset += chunk;
        size -= chunk;
    }
    return 1;
}
/**
 * @brief Normalize mutable fields before hashing a complete record header.
 * @param record Decoded header scratch.
 * @return Computed IEEE CRC32.
 * @details Temporarily zero CRC and set commit/deleted to all ones, then restore the caller copy.
 */
static uint32_t record_crc(microapp_store_record_t *record)
{
    uint32_t crc = record->crc32, commit = record->commit, deleted = record->deleted;
    record->crc32 = 0u;
    record->commit = record->deleted = UINT32_MAX;
    uint32_t result = microapp_crc32((const uint8_t *)record, sizeof(*record));
    record->crc32 = crc;
    record->commit = commit;
    record->deleted = deleted;
    return result;
}
/**
 * @brief Hash journal metadata without its mutable commit/consumed markers.
 * @param journal Decoded durable journal metadata.
 * @return Computed IEEE CRC32.
 */
static uint32_t journal_crc(journal_t *journal)
{
    journal_t copy = *journal;
    copy.header_crc32 = 0u;
    copy.commit = copy.consumed = UINT32_MAX;
    return microapp_crc32((const uint8_t *)&copy, sizeof(copy));
}
/**
 * @brief Write durably, then read back every byte before accepting success.
 * @param region Borrowed storage callbacks.
 * @param offset Region-relative byte offset.
 * @param data Stable source bytes.
 * @param size Range size in bytes.
 * @return OK or IO, including bounds/write/readback mismatch.
 * @details An error does not imply the storage was unchanged.
 */
static int write_checked(microapp_storage_t *region, uint32_t offset, const void *data, uint32_t size)
{
    const uint8_t *src = data;
    uint8_t buf[256];
    if (offset > region->size || size > region->size - offset ||
        region->write(region->user, offset, data, size)) return MICROAPP_STORE_IO;
    while (size) {
        uint32_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (region->read(region->user, offset, buf, chunk) || memcmp(buf, src, chunk))
            return MICROAPP_STORE_IO;
        offset += chunk;
        src += chunk;
        size -= chunk;
    }
    return 0;
}
/**
 * @brief Copy bounded chunks to an already erased destination.
 * @param to Already erased destination region.
 * @param to_offset Destination region offset.
 * @param from Borrowed source region.
 * @param from_offset Source region offset.
 * @param size Range size in bytes.
 * @return OK or IO.
 * @details Skip all-FF chunks; both callers have erased the destination first.
 */
static int copy_region(microapp_storage_t *to, uint32_t to_offset,
    microapp_storage_t *from, uint32_t from_offset, uint32_t size)
{
    uint8_t buf[256];
    while (size) {
        uint32_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (from->read(from->user, from_offset, buf, chunk)) return MICROAPP_STORE_IO;
        bool all_ff = true;
        for (uint32_t i = 0u; i < chunk; ++i) if (buf[i] != 0xffu) { all_ff = false; break; }
        /* Both copy destinations have just been erased. Skip erased padding. */
        if (!all_ff) {
            int rc = write_checked(to, to_offset, buf, chunk);
            if (rc) return rc;
        }
        from_offset += chunk;
        to_offset += chunk;
        size -= chunk;
    }
    return 0;
}
/**
 * @brief Verify committed backup, erase/rewrite primary, verify again and mark consumed.
 * @param store Host-owned store handle.
 * @param journal Decoded durable journal metadata.
 * @return OK, CORRUPT or IO.
 * @details A pending durable backup remains the recovery authority until consumed is zero.
 */
static int restore(microapp_store_t *store, journal_t *journal)
{
    uint32_t crc;
    if (journal->magic != MICROAPP_STORE_JOURNAL_MAGIC || journal->version != 1u ||
        journal->size != store->flash.size || journal->header_crc32 != journal_crc(journal))
        return MICROAPP_STORE_CORRUPT;
    int rc = region_crc(&store->backup, MICROAPP_STORE_JOURNAL_SIZE, journal->size, &crc);
    if (rc) return rc;
    if (crc != journal->crc32) return MICROAPP_STORE_CORRUPT;
    if (store->flash.erase(store->flash.user, 0u, store->flash.size)) return MICROAPP_STORE_IO;
    rc = copy_region(&store->flash, 0u, &store->backup, MICROAPP_STORE_JOURNAL_SIZE, journal->size);
    if (rc) return rc;
    rc = region_crc(&store->flash, 0u, journal->size, &crc);
    if (rc) return rc;
    if (crc != journal->crc32) return MICROAPP_STORE_CORRUPT;
    uint32_t zero = 0u;
    return write_checked(&store->backup, offsetof(journal_t, consumed), &zero, sizeof(zero));
}
/** @brief Initialize callbacks and recover any committed unfinished backup before returning.
 * @see microapp_store_open
 */
int microapp_store_open(microapp_store_t *store, const microapp_storage_t *flash,
    const microapp_storage_t *backup, uint32_t address, const microapp_target_t *target)
{
    if (!store || !flash || !flash->read || !flash->write || !flash->erase ||
        flash->size < 512u || flash->size > UINT32_MAX - MICROAPP_STORE_JOURNAL_SIZE ||
        address > UINT32_MAX - flash->size || !target) return MICROAPP_STORE_INVALID;
    memset(store, 0, sizeof(*store));
    store->flash = *flash;
    store->target = *target;
    store->flash_address = address;
    if (backup) {
        if (!backup->read || !backup->write || !backup->erase ||
            backup->size < flash->size + MICROAPP_STORE_JOURNAL_SIZE) return MICROAPP_STORE_INVALID;
        store->backup = *backup;
        journal_t journal;
        if (backup->read(backup->user, 0u, &journal, sizeof(journal))) return MICROAPP_STORE_IO;
        if (journal.commit == 0u && journal.consumed != 0u) {
            int rc = restore(store, &journal);
            if (rc) return rc;
        }
    }
    store->ready = true;
    return 0;
}
/* 0 = record; 1 = clean end; 2 = interrupted/invalid tail. Legacy format-2
 * single slot is exposed as one record, then migrated during first compaction. */
/**
 * @brief Classify a record, erased end, interrupted tail or legacy 2.0 single slot.
 * @param store Host-owned store handle.
 * @param offset Region-relative byte offset.
 * @param record Decoded header scratch.
 * @return 0 for record, 1 for clean end, 2 for blocked tail, or negative store error.
 * @details Committed malformed headers fail closed; uncommitted tail can only be reclaimed by GC.
 */
static int record_at(microapp_store_t *store, uint32_t offset, microapp_store_record_t *record)
{
    if (offset > store->flash.size || sizeof(*record) > store->flash.size - offset) {
        int rc = blank(&store->flash, offset, store->flash.size - offset);
        return rc < 0 ? rc : rc ? 1 : 2;
    }
    if (store->flash.read(store->flash.user, offset, record, sizeof(*record))) return MICROAPP_STORE_IO;
    if (record->magic == MICROAPP_STORE_RECORD_MAGIC && record->commit == 0u &&
        (record->version != 1u || record->crc32 != record_crc(record))) return MICROAPP_STORE_CORRUPT;
    if (offset == 0u && record->magic == MICROAPP_SLOT_MAGIC) {
        microapp_slot_header_t old;
        memcpy(&old, record, sizeof(old));
        uint32_t crc = old.header_crc32;
        old.header_crc32 = 0u;
        if (old.format_major != 2u || old.format_minor != 0u || old.header_size != sizeof(old) ||
            old.manifest_size != sizeof(old.manifest) ||
            microapp_crc32((const uint8_t *)&old, sizeof(old)) != crc ||
            microapp_validate_manifest(&old.manifest, &store->target) ||
            old.image_offset < 512u || old.image_offset > store->flash.size ||
            old.body_size > store->flash.size - old.image_offset || old.body_size != old.manifest.body_size)
            return MICROAPP_STORE_CORRUPT;
        memset(record, 0xff, sizeof(*record));
        record->magic = MICROAPP_SLOT_MAGIC;
        record->span = store->flash.size;
        record->body_offset = old.image_offset;
        record->manifest = old.manifest;
        record->commit = 0u;
        return 0;
    }
    if (record->magic != MICROAPP_STORE_RECORD_MAGIC || record->version != 1u ||
        record->span < 512u || (record->span & 31u) || record->span > store->flash.size - offset ||
        record->body_offset < 512u || record->body_offset > record->span ||
        record->manifest.body_size > record->span - record->body_offset ||
        record->crc32 != record_crc(record) ||
        microapp_validate_manifest(&record->manifest, &store->target) ||
        ((store->flash_address + offset + record->body_offset) & (record->manifest.image_alignment - 1u))) {
        /* Committed data is never classified as disposable interrupted tail. */
        if (record->magic == MICROAPP_STORE_RECORD_MAGIC && record->commit == 0u)
            return MICROAPP_STORE_CORRUPT;
        int rc = blank(&store->flash, offset, store->flash.size - offset);
        return rc < 0 ? rc : rc ? 1 : 2;
    }
    return 0;
}
/**
 * @brief Copy metadata and convert record-relative body offset to region-relative offset.
 * @param offset Region-relative byte offset.
 * @param record Decoded header scratch.
 * @param entry Temporary copied record metadata.
 */
static void make_entry(uint32_t offset, const microapp_store_record_t *record, microapp_store_entry_t *entry)
{
    entry->offset = offset;
    entry->body_offset = offset + record->body_offset;
    entry->span = record->span;
    entry->manifest = record->manifest;
}
/**
 * @brief Scan committed versions of one ID without validating the selected body yet.
 * @param store Host-owned store handle.
 * @param id NUL-terminated App ID.
 * @param entry Temporary copied record metadata.
 * @return OK, NOT_FOUND or scan error.
 * @details Latest deleted version hides all older versions; corrupt latest data is not rollback.
 */
static int find_raw(microapp_store_t *store, const char *id, microapp_store_entry_t *entry)
{
    bool found = false;
    uint32_t offset = 0u;
    while (offset < store->flash.size) {
        microapp_store_record_t record;
        int rc = record_at(store, offset, &record);
        if (rc < 0) return rc;
        if (rc) break;
        if (record.commit == 0u && strcmp(record.manifest.app_id, id) == 0) {
            found = record.deleted == UINT32_MAX;
            if (found) make_entry(offset, &record, entry);
        }
        offset += record.span;
    }
    return found ? 0 : MICROAPP_STORE_NOT_FOUND;
}
/**
 * @brief Validate compatibility, actual code alignment and the complete selected body.
 * @param store Host-owned store handle.
 * @param entry Temporary copied record metadata.
 * @return OK or CORRUPT.
 * @details Native reader errors are intentionally collapsed into CORRUPT in this store path.
 */
static int validate_entry(microapp_store_t *store, const microapp_store_entry_t *entry)
{
    if (microapp_validate_manifest(&entry->manifest, &store->target) ||
        ((store->flash_address + entry->body_offset) & (entry->manifest.image_alignment - 1u)))
        return MICROAPP_STORE_CORRUPT;
    slice_t slice = {&store->flash, entry->body_offset};
    microapp_reader_t body = {read_slice, &slice, entry->manifest.body_size};
    return microapp_validate_body(&entry->manifest, &body) ? MICROAPP_STORE_CORRUPT : 0;
}
/** @brief Find and validate the newest committed live record for an App ID.
 * @see microapp_store_find
 */
int microapp_store_find(microapp_store_t *store, const char *id, microapp_store_entry_t *entry)
{
    if (!store || !store->ready || !id || !entry) return MICROAPP_STORE_INVALID;
    int rc = find_raw(store, id, entry);
    return rc ? rc : validate_entry(store, entry);
}
/** @brief Enumerate and validate latest live records in physical order.
 * @see microapp_store_foreach
 */
int microapp_store_foreach(microapp_store_t *store, microapp_store_visit_fn visit, void *user)
{
    if (!store || !store->ready || !visit) return MICROAPP_STORE_INVALID;
    uint32_t offset = 0u;
    while (offset < store->flash.size) {
        microapp_store_record_t record;
        int rc = record_at(store, offset, &record);
        if (rc < 0) return rc;
        if (rc) break;
        if (record.commit == 0u && record.deleted == UINT32_MAX) {
            microapp_store_entry_t latest;
            rc = find_raw(store, record.manifest.app_id, &latest);
            if (rc != MICROAPP_STORE_NOT_FOUND && rc) return rc;
            if (!rc && latest.offset == offset) {
                rc = validate_entry(store, &latest);
                if (rc) return rc;
                rc = visit(user, &latest);
                if (rc) return rc;
            }
        }
        offset += record.span;
    }
    return 0;
}
/**
 * @brief Accumulate latest live record count and physical spans.
 * @param user Borrowed adapter/visitor context.
 * @param entry Temporary copied record metadata.
 * @return Zero to continue enumeration.
 */
static int count_entry(void *user, const microapp_store_entry_t *entry)
{
    microapp_store_usage_t *usage = user;
    ++usage->count;
    usage->live_bytes += entry->span;
    return 0;
}
/** @brief Validate live bodies and compute occupied, appendable and reclaimable bytes.
 * @see microapp_store_usage
 */
int microapp_store_usage(microapp_store_t *store, microapp_store_usage_t *usage)
{
    if (!store || !store->ready || !usage) return MICROAPP_STORE_INVALID;
    memset(usage, 0, sizeof(*usage));
    uint32_t offset = 0u;
    while (offset < store->flash.size) {
        microapp_store_record_t record;
        int rc = record_at(store, offset, &record);
        if (rc < 0) return rc;
        if (rc == 1) break;
        if (rc == 2) { offset = store->flash.size; usage->needs_compaction = true; break; }
        if (record.magic == MICROAPP_SLOT_MAGIC) usage->needs_compaction = true;
        offset += record.span;
    }
    usage->used_bytes = offset;
    usage->free_bytes = store->flash.size - offset;
    int rc = microapp_store_foreach(store, count_entry, usage);
    if (rc) return rc;
    usage->reclaimable_bytes = usage->used_bytes - usage->live_bytes;
    return 0;
}
/**
 * @brief Compute aligned body/span using wide arithmetic, then create an uncommitted header.
 * @param store Host-owned store handle.
 * @param offset Region-relative byte offset.
 * @param manifest Validated metadata.
 * @param preferred Minimum record-relative body offset.
 * @param record Decoded header scratch.
 * @return OK or FULL.
 * @details The caller validates manifest alignment before using this helper.
 */
static int layout(microapp_store_t *store, uint32_t offset, const microapp_manifest_t *manifest,
    uint32_t preferred, microapp_store_record_t *record)
{
    uint32_t alignment = manifest->image_alignment;
    uint64_t body = (uint64_t)store->flash_address + offset + preferred;
    body = (body + alignment - 1u) & ~((uint64_t)alignment - 1u);
    body -= (uint64_t)store->flash_address + offset;
    uint64_t span = (body + manifest->body_size + 31u) & ~UINT64_C(31);
    if (offset > store->flash.size || span > store->flash.size - offset) return MICROAPP_STORE_FULL;
    memset(record, 0xff, sizeof(*record));
    record->magic = MICROAPP_STORE_RECORD_MAGIC;
    record->version = 1u;
    record->span = (uint32_t)span;
    record->body_offset = (uint32_t)body;
    record->manifest = *manifest;
    record->crc32 = record_crc(record);
    return 0;
}
/** @brief Compaction visitor state; no heap index or full image buffer. */
typedef struct {
    microapp_store_t *store; /**< Active source and destination callbacks. */
    uint32_t offset; /**< Next compacted record position. */
} compact_t;
/**
 * @brief Write one resized committed record and unchanged body into the erased backup.
 * @param user Borrowed adapter/visitor context.
 * @param entry Temporary copied record metadata.
 * @return OK or layout/copy/write error.
 * @details Backup records become authoritative only after the journal itself commits.
 */
static int compact_entry(void *user, const microapp_store_entry_t *entry)
{
    compact_t *context = user;
    microapp_store_t *store = context->store;
    microapp_store_record_t record;
    int rc = layout(store, context->offset, &entry->manifest, 512u, &record);
    if (rc) return rc;
    record.commit = 0u;
    uint32_t destination = MICROAPP_STORE_JOURNAL_SIZE + context->offset;
    rc = write_checked(&store->backup, destination, &record, sizeof(record));
    if (rc) return rc;
    rc = copy_region(&store->backup, destination + record.body_offset,
        &store->flash, entry->body_offset, entry->manifest.body_size);
    if (rc) return rc;
    context->offset += record.span;
    return 0;
}
/** @brief Commit a verified independent backup, then restore compacted records to primary Flash.
 * @see microapp_store_compact
 */
int microapp_store_compact(microapp_store_t *store)
{
    if (!store || !store->ready || !store->backup.read) return MICROAPP_STORE_INVALID;
    if (store->backup.erase(store->backup.user, 0u, store->flash.size + MICROAPP_STORE_JOURNAL_SIZE))
        return MICROAPP_STORE_IO;
    compact_t context = {store, 0u};
    int rc = microapp_store_foreach(store, compact_entry, &context);
    if (rc) return rc;
    journal_t journal = {MICROAPP_STORE_JOURNAL_MAGIC, 1u, store->flash.size, 0u, 0u, UINT32_MAX, UINT32_MAX};
    rc = region_crc(&store->backup, MICROAPP_STORE_JOURNAL_SIZE, store->flash.size, &journal.crc32);
    if (rc) return rc;
    journal.header_crc32 = journal_crc(&journal);
    rc = write_checked(&store->backup, 0u, &journal, sizeof(journal));
    if (rc) return rc;
    uint32_t zero = 0u;
    /* After this commit, every failure requires recovery before another write. */
    store->ready = false;
    rc = write_checked(&store->backup, offsetof(journal_t, commit), &zero, sizeof(zero));
    if (rc) return rc;
    journal.commit = 0u;
    rc = restore(store, &journal);
    if (!rc) store->ready = true;
    return rc;
}
/* Keep sizing scratch frames off the stack while compaction is copying records. */
/**
 * @brief Predict compacted live footprint before permitting any storage mutation.
 * @param store Host-owned store handle.
 * @param size Range size in bytes.
 * @return OK or scan/layout error.
 * @details Kept noinline so sizing scratch is not live during the deeper compaction call chain.
 */
static __attribute__((noinline)) int compacted_size(microapp_store_t *store, uint32_t *size)
{
    *size = 0u;
    uint32_t offset = 0u;
    while (offset < store->flash.size) {
        microapp_store_record_t current;
        int rc = record_at(store, offset, &current);
        if (rc < 0) return rc;
        if (rc) break;
        if (current.commit == 0u && current.deleted == UINT32_MAX) {
            microapp_store_entry_t latest;
            rc = find_raw(store, current.manifest.app_id, &latest);
            if (rc && rc != MICROAPP_STORE_NOT_FOUND) return rc;
            if (!rc && latest.offset == offset) {
                microapp_store_record_t resized;
                rc = layout(store, *size, &latest.manifest, 512u, &resized);
                if (rc) return rc;
                *size += resized.span;
            }
        }
        offset += current.span;
    }
    return 0;
}
/** @brief Append a verified immutable body and commit the new ID version last.
 * @see microapp_store_install
 */
int microapp_store_install(microapp_store_t *store, const microapp_manifest_t *manifest,
    const microapp_reader_t *body, uint32_t preferred)
{
    if (!store || !store->ready || !manifest || !body || preferred < 512u ||
        preferred > store->flash.size || microapp_validate_manifest(manifest, &store->target) ||
        microapp_validate_body(manifest, body)) return MICROAPP_STORE_INVALID;
    microapp_store_usage_t usage;
    int rc = microapp_store_usage(store, &usage);
    if (rc) return rc;
    microapp_store_record_t record;
    rc = layout(store, usage.used_bytes, manifest, preferred, &record);
    if (rc || usage.needs_compaction) {
        /* Prove the new record fits after compaction before erasing anything. */
        uint32_t size;
        rc = compacted_size(store, &size);
        if (rc) return rc;
        rc = layout(store, size, manifest, preferred, &record);
        if (rc) return rc;
        if (!store->backup.read) return MICROAPP_STORE_FULL;
        rc = microapp_store_compact(store);
        if (rc) return rc;
        rc = microapp_store_usage(store, &usage);
        if (rc) return rc;
        rc = layout(store, usage.used_bytes, manifest, preferred, &record);
        if (rc) return rc;
    }
    rc = write_checked(&store->flash, usage.used_bytes, &record, sizeof(record));
    if (rc) return rc;
    uint8_t buf[256];
    for (uint32_t done = 0u; done < manifest->body_size;) {
        uint32_t size = manifest->body_size - done;
        if (size > sizeof(buf)) size = sizeof(buf);
        if (body->read(body->user, done, buf, size)) return MICROAPP_STORE_IO;
        rc = write_checked(&store->flash, usage.used_bytes + record.body_offset + done, buf, size);
        if (rc) return rc;
        done += size;
    }
    slice_t slice = {&store->flash, usage.used_bytes + record.body_offset};
    microapp_reader_t installed = {read_slice, &slice, manifest->body_size};
    if (microapp_validate_body(manifest, &installed)) return MICROAPP_STORE_CORRUPT;
    uint32_t zero = 0u;
    /* Latest committed record wins; prior versions survive an interrupted update. */
    return write_checked(&store->flash, usage.used_bytes + offsetof(microapp_store_record_t, commit), &zero, sizeof(zero));
}
/** @brief Mark the newest live version deleted without reviving older versions.
 * @see microapp_store_remove
 */
int microapp_store_remove(microapp_store_t *store, const char *id)
{
    if (!store || !store->ready || !id) return MICROAPP_STORE_INVALID;
    microapp_store_entry_t entry;
    int rc = microapp_store_find(store, id, &entry);
    if (rc) return rc;
    microapp_store_record_t record;
    rc = record_at(store, entry.offset, &record);
    if (rc) return rc;
    if (record.magic == MICROAPP_SLOT_MAGIC) {
        rc = microapp_store_compact(store);
        if (rc) return rc;
        rc = microapp_store_find(store, id, &entry);
        if (rc) return rc;
    }
    uint32_t zero = 0u;
    return write_checked(&store->flash, entry.offset + offsetof(microapp_store_record_t, deleted), &zero, sizeof(zero));
}
