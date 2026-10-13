/**
 * @brief Bounded package validation, instance RAM preparation and ARM calling bridge.
 * @file microapp_native.h
 * @defgroup microapp_native Native loader primitives
 * @{
 */
#ifndef MICROAPP_NATIVE_H
#define MICROAPP_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include "microapp_format.h"
#include "microapp_abi_port.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read exactly size bytes from a reader-relative offset.
 * @param[in] user Borrowed reader context.
 * @param[in] offset Reader-relative byte offset.
 * @param[out] dst Writable buffer of at least size bytes.
 * @param[in] size Exact read byte count.
 * @return Zero after copying all bytes, nonzero for failure; short reads are errors.
 */
typedef int (*microapp_read_fn)(void *user, uint32_t offset, void *dst, uint32_t size);
/**
 * @brief Borrowed bounded random-access source; content stays stable during each operation.
 */
typedef struct {
    microapp_read_fn read; /**< Exact-range read callback. */
    void *user; /**< Borrowed reader context. */
    uint32_t size; /**< Total readable bytes. */
} microapp_reader_t;
/**
 * @brief Host compatibility policy, independent of the framework release version.
 */
typedef struct {
    const char *board_id; /**< Borrowed NUL-terminated host profile ID. */
    uint32_t architecture; /**< Required MICROAPP_ARCH_ARMV7EM architecture. */
    uint32_t float_abi; /**< Required MICROAPP_FLOAT_* calling convention. */
    uint32_t abi_major; /**< Required equal host ABI major. */
    uint32_t abi_minor; /**< Highest supported App ABI minor. */
    uint32_t host_version; /**< Current host version. */
    uint32_t permissions; /**< Host-allowed permission bits. */
    uint32_t max_body_size; /**< Maximum immutable-body bytes. */
    uint32_t max_ram_size; /**< Maximum instance RAM bytes. */
} microapp_target_t;
/** @brief Module-specific result codes, distinct from manager/native ABI results. */
enum { MICROAPP_NATIVE_OK=0 /**< Validated/prepared successfully. */, MICROAPP_NATIVE_INVALID=-1 /**< Invalid layout/CRC/placement; some read errors are collapsed here. */,
       MICROAPP_NATIVE_UNSUPPORTED=-2 /**< Unsupported format/architecture or incompatible host policy. */, MICROAPP_NATIVE_IO=-3 /**< Read callback failed in a path preserving I/O errors. */ };
/**
 * @brief Validate metadata text, ABI policy and all body/RAM layout bounds.
 * @param[in] m Decoded format-2.0 manifest.
 * @param[in] t Host policy; NULL checks only structural/architecture rules.
 * @return OK, INVALID for structure, or UNSUPPORTED for architecture/host compatibility.
 * @note No body data or signatures are read.
 */
int microapp_validate_manifest(const microapp_manifest_t *m, const microapp_target_t *t);
/**
 * @brief Verify immutable body CRCs, ordered RAM fixups and resource table bounds.
 * @param[in] m Manifest; structural constraints are also checked.
 * @param[in] r Stable exact body reader, with size equal to m->body_size.
 * @return OK, INVALID, UNSUPPORTED or IO; CRC read failures currently collapse to INVALID.
 * @note Host target policy is checked separately by microapp_validate_manifest().
 */
int microapp_validate_body(const microapp_manifest_t *m, const microapp_reader_t *r);
/**
 * @brief Validate complete package layout, metadata and body without writing storage.
 * @param[in] r Stable whole-package reader, including optional signature bytes.
 * @param[in] t Host compatibility policy; NULL skips host-specific checks.
 * @param[out] h Decoded package header, authoritative only on success.
 * @param[out] m Decoded manifest, authoritative only on success.
 * @return OK, INVALID, UNSUPPORTED or IO; header/CRC read failures may return INVALID.
 * @warning CRC is integrity only. Signature bytes are opaque; host authenticates them separately.
 */
int microapp_validate_package(const microapp_reader_t *r, const microapp_target_t *t,
                             microapp_package_header_t *h, microapp_manifest_t *m);

/**
 * @brief Validate, zero instance RAM, copy initialization bytes and apply RAM-only fixups.
 * @param[in] m Manifest; host target policy must already have been checked.
 * @param[in] r Stable exact immutable-body reader.
 * @param[out] ram Writable host buffer aligned to ram_alignment.
 * @param[in] capacity Buffer byte capacity, at least m->ram_size.
 * @param[in] flash Actual 32-bit ARM address of body byte zero, aligned to image_alignment.
 * @param[in] address Actual 32-bit ARM instance address, aligned to ram_alignment.
 * @return OK, INVALID, UNSUPPORTED or IO.
 * @details Explicit ARM addresses permit pointer tests on a 64-bit PC. Validation/placement
 * failure precedes RAM mutation; later read failure may leave partially prepared RAM.
 * @warning Enter App code only on success. Reprepare on every CREATE; Flash is never changed.
 */
int microapp_prepare_ram(const microapp_manifest_t *m, const microapp_reader_t *r,
                        void *ram, uint32_t capacity, uint32_t flash,
                        uint32_t address);

/**
 * @brief Call a Thumb function with the instance GOT in r9 and restore the caller r9.
 * @param[in] got Actual linker-defined GOT base: RAM address plus manifest.got_offset.
 * @param[in] function Valid Thumb entry/callback address, preserving the low Thumb bit.
 * @param[in] arg0 First AAPCS argument: canonical entry host_api.
 * @param[in] arg1 Second argument: canonical entry context.
 * @param[in] arg2 Third argument: canonical entry reason.
 * @param[in] arg3 Fourth argument: host-defined payload.
 * @return Raw r0 value; cast to int32_t for canonical entry results.
 * @warning ARM hosts only. Caller supplies a valid prepared instance and an AAPCS
 * aligned stack. This bridge provides neither address validation nor memory isolation.
 */
uint32_t microapp_arm_call(uintptr_t got, uintptr_t function, uintptr_t arg0,
                          uintptr_t arg1, uintptr_t arg2, uintptr_t arg3);
#ifdef __cplusplus
}
#endif
#endif
/** @} */
