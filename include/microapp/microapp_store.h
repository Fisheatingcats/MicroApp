/**
 * @brief Append-only multi-App storage with immutable bodies and recoverable compaction.
 * @file microapp_store.h
 * @defgroup microapp_store Multi-App persistent storage
 * @{
 */
#ifndef MICROAPP_STORE_H
#define MICROAPP_STORE_H
#include <stdbool.h>
#include "microapp_native.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MICROAPP_STORE_HEADER_SIZE 512u /**< Persistent MPAR record header bytes, including reserved padding. */
#define MICROAPP_STORE_JOURNAL_SIZE 4096u /**< Backup-region prefix bytes reserved for the recovery journal. */
#define MICROAPP_STORE_RECORD_MAGIC UINT32_C(0x5241504d) /**< Little-endian MPAR multi-App record identifier. */
#define MICROAPP_STORE_JOURNAL_MAGIC UINT32_C(0x4a41504d) /**< Little-endian MPAJ committed-backup journal identifier. */
/**
 * @brief Persist an exact bounded NOR write before returning.
 * @param[in] user Borrowed storage context.
 * @param[in] offset Region-relative byte offset.
 * @param[in] data Stable bytes to program.
 * @param[in] size Exact write byte count.
 * @return Zero on durable success; nonzero may still mean partially changed storage.
 * @warning Only 1 to 0 writes; host must provide alignment/granularity adaptation.
 */
typedef int (*microapp_write_fn)(void *user, uint32_t offset, const void *data, uint32_t size);
/**
 * @brief Durably restore a region range to FF.
 * @param[in] user Borrowed storage context.
 * @param[in] offset Region-relative erase start.
 * @param[in] size Erase byte count.
 * @return Zero on durable success, nonzero on failure (possibly after partial erase).
 * @note Core compaction erases the whole primary region and flash.size + 4096 backup bytes.
 * The host maps these logical requests to physical erase units.
 */
typedef int (*microapp_erase_fn)(void *user, uint32_t offset, uint32_t size);

/**
 * @brief Borrowed synchronous NOR-like storage callbacks; offsets are region-relative.
 */
typedef struct {
    microapp_read_fn read; /**< Exact bounded read callback. */
    microapp_write_fn write; /**< Durable NOR write callback. */
    microapp_erase_fn erase; /**< Durable erase callback. */
    void *user; /**< Borrowed storage driver context. */
    uint32_t size; /**< Region size in bytes. */
} microapp_storage_t;
/**
 * @brief 512-byte persistent container header; normalized CRC excludes mutable commit/delete bits.
 */
typedef struct {
    uint32_t magic; /**< MICROAPP_STORE_RECORD_MAGIC (MPAR). */
    uint32_t version; /**< Container version 1; independent of package version 2.0. */
    uint32_t span; /**< Whole record bytes, aligned to 32; next offset = offset + span. */
    uint32_t body_offset; /**< Record-relative immutable-body offset, at least 512. */
    uint32_t crc32; /**< Header CRC with crc32 zero and commit/deleted all ones. */
    uint32_t commit; /**< FFFFFFFF while preparing; zero is committed. */
    uint32_t deleted; /**< FFFFFFFF is live; any cleared bit hides this committed version. */
    microapp_manifest_t manifest; /**< Copied manifest; body and RAM offsets retain their format-2.0 meanings. */
    uint8_t reserved[512u - 28u - sizeof(microapp_manifest_t)]; /**< Erased padding in header CRC. */
} microapp_store_record_t;
/**
 * @brief Copied enumeration result; offsets become stale after mutation/compaction.
 */
typedef struct {
    uint32_t offset; /**< Record start relative to the Flash region. */
    uint32_t body_offset; /**< Body start relative to the Flash region, unlike record header body_offset. */
    uint32_t span; /**< Whole record bytes. */
    microapp_manifest_t manifest; /**< Copied manifest; body and RAM offsets retain their format-2.0 meanings. */
} microapp_store_entry_t;
/**
 * @brief Validated physical and live-space accounting; byte counts include record padding.
 */
typedef struct {
    uint32_t count; /**< Number of latest live App IDs. */
    uint32_t live_bytes; /**< Sum of spans of latest live records. */
    uint32_t used_bytes; /**< Physical occupied prefix; invalid tail reserves remaining region. */
    uint32_t free_bytes; /**< Clean appendable tail bytes. */
    uint32_t reclaimable_bytes; /**< used_bytes minus live_bytes; alignment compaction may reclaim more. */
    bool needs_compaction; /**< Legacy single slot or invalid/interrupted tail blocks normal append. */
} microapp_store_usage_t;
/**
 * @brief Host-owned store handle; serialize calls and reopen after interrupted committed compaction.
 */
typedef struct {
    microapp_storage_t flash; /**< Primary callbacks; driver context remains borrowed. */
    microapp_storage_t backup; /**< Backup callbacks; zeroed if GC disabled. */
    microapp_target_t target; /**< Copied policy; target.board_id remains borrowed. */
    uint32_t flash_address; /**< Actual ARM address of region byte zero, used for alignment. */
    bool ready; /**< Operations allowed; false while committed backup needs recovery. */
} microapp_store_t;
/** @brief Module-specific result codes, distinct from manager/native ABI results. */
enum { MICROAPP_STORE_OK=0 /**< Storage operation completed. */, MICROAPP_STORE_INVALID=-1 /**< Invalid handle/argument/new body or unavailable backup. */, MICROAPP_STORE_IO=-2 /**< Driver or readback failed; writes may already be durable. */,
       MICROAPP_STORE_FULL=-3 /**< No room for safe append, even after available compaction. */, MICROAPP_STORE_NOT_FOUND=-4 /**< No latest live record for this ID. */, MICROAPP_STORE_CORRUPT=-5 /**< Committed metadata/body/backup invalid; never silently erased. */ };
/**
 * @brief Observe one temporary copied latest-live entry during enumeration.
 * @param[in] user Borrowed visitor context.
 * @param[in] entry Temporary result; copy before retaining it.
 * @return Zero to continue, nonzero to stop and propagate the result.
 * @warning Do not mutate the store from this callback.
 */
typedef int (*microapp_store_visit_fn)(void *user, const microapp_store_entry_t *entry);

/**
 * @brief Initialize callbacks and recover any committed unfinished backup before returning.
 * @param[out] store Caller-owned handle; previous content is replaced.
 * @param[in] flash Primary storage, at least 512 bytes.
 * @param[in] backup Independent region of at least flash.size + 4096; NULL disables GC.
 * @param[in] address Actual ARM region base; no physical address is stored in records.
 * @param[in] target Host policy, copied by value; its board_id string remains borrowed.
 * @return OK, INVALID, IO or CORRUPT; use find/foreach/usage to validate main records.
 * @warning Even open may erase/rewrite primary Flash during recovery. No App may
 * execute from this region. All callbacks must complete synchronous durable I/O.
 * @note Primary and backup regions must not overlap; host serializes all calls.
 */
int microapp_store_open(microapp_store_t *store, const microapp_storage_t *flash,
    const microapp_storage_t *backup, uint32_t address, const microapp_target_t *target);
/**
 * @brief Find and validate the newest committed live record for an App ID.
 * @param[in,out] store Ready handle.
 * @param[in] id NUL-terminated ID.
 * @param[out] entry Copied result, authoritative only on success.
 * @return OK, NOT_FOUND (including tombstoned ID), INVALID, IO or CORRUPT.
 * @note A corrupt newest version is not silently replaced by an older version.
 */
int microapp_store_find(microapp_store_t *store, const char *id, microapp_store_entry_t *entry);
/**
 * @brief Enumerate and validate latest live records in physical order.
 * @param[in,out] store Ready handle.
 * @param[in] visit Callback; return zero to continue, nonzero to stop.
 * @param[in] user Borrowed visitor context.
 * @return OK, a store error, or the first nonzero visitor result.
 * @warning entry is temporary; copy it if needed later. Visitor must not mutate
 * the store. Enumeration scans latest versions in O(n squared) without a heap index.
 */
int microapp_store_foreach(microapp_store_t *store, microapp_store_visit_fn visit, void *user);
/**
 * @brief Validate live bodies and compute occupied, appendable and reclaimable bytes.
 * @param[in,out] store Ready handle.
 * @param[out] usage Copied accounting; authoritative only on success.
 * @return OK, INVALID, IO or CORRUPT.
 * @note This scans storage; it is not a constant-time cached query. Deleted space
 * is not appendable until compacted.
 */
int microapp_store_usage(microapp_store_t *store, microapp_store_usage_t *usage);
/**
 * @brief Append a verified immutable body and commit the new ID version last.
 * @param[in,out] store Ready handle; host must prevent native execution throughout mutation.
 * @param[in] manifest Decoded metadata matching target and body.
 * @param[in] body Stable exact body reader; must remain readable across possible GC.
 * @param[in] preferred Minimum record-relative body offset, at least 512; aligned upward.
 * @return OK, INVALID, FULL, IO or CORRUPT.
 * @details Source must not alias primary/backup regions that GC can overwrite.
 * Validation and fit checks precede mutation. Updating needs old plus new space;
 * latest committed version wins. Bad new metadata/body is reported as INVALID.
 * @note I/O failure may occur after a commit became durable: rescan/reopen before
 * retrying. No per-operation rollback or signature authentication is provided.
 */
int microapp_store_install(microapp_store_t *store, const microapp_manifest_t *manifest,
    const microapp_reader_t *body, uint32_t preferred);
/**
 * @brief Mark the newest live version deleted without reviving older versions.
 * @param[in,out] store Ready handle; host prevents native execution.
 * @param[in] id NUL-terminated App ID.
 * @return OK, NOT_FOUND, INVALID, IO or CORRUPT.
 * @note Deletion does not erase body bytes or return appendable space. Legacy
 * 2.0 single-slot deletion first migrates by compaction, requiring backup.
 * A partial delete write may already hide the target even if IO is returned.
 */
int microapp_store_remove(microapp_store_t *store, const char *id);
/**
 * @brief Commit a verified independent backup, then restore compacted records to primary Flash.
 * @param[in,out] store Ready handle with backup callbacks and no executing native App.
 * @return OK, INVALID when no backup/ready handle, FULL, IO or CORRUPT.
 * @details Latest live bodies are copied unchanged; record positions may move.
 * Backup commit precedes primary erase; successful restore marks backup consumed.
 * Once backup commit is attempted, failures leave ready false: call open to recover.
 * Discard all cached positions and re-enumerate after success/recovery.
 * @warning No locks, wear levelling or physical power-loss guarantee is supplied
 * independently of the host driver durability contract.
 */
int microapp_store_compact(microapp_store_t *store);
#ifdef __cplusplus
}
#endif
#endif
/** @} */
