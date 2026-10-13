/**
 * @brief Little-endian packed package, resource and RAM-fixup layouts; no final installation addresses.
 * @file microapp_format.h
 * @defgroup microapp_format Wire format 2.0
 * @{
 */
#ifndef MICROAPP_FORMAT_H
#define MICROAPP_FORMAT_H
#include <stdint.h>
#define MICROAPP_PACKAGE_MAGIC 0x5050414Du /**< Little-endian MAPP package identifier. */
#define MICROAPP_SLOT_MAGIC 0x4C53414Du /**< Little-endian MASL legacy single-slot identifier; not the multi-App container. */
#define MICROAPP_FORMAT_MAJOR 2u /**< Supported package major version; format 1 is rejected. */
#define MICROAPP_FORMAT_MINOR 0u /**< Supported package minor version within major 2. */
#define MICROAPP_SLOT_HEADER_SIZE 512u /**< Legacy single-slot header bytes, including reserved padding. */
#define MICROAPP_ID_MAX 32u /**< App ID field bytes, including its required NUL terminator. */
#define MICROAPP_NAME_MAX 32u /**< Display-name field bytes, including its required NUL terminator. */
#define MICROAPP_VERSION_MAX 16u /**< Display-version field bytes, including its required NUL terminator. */
#define MICROAPP_AUTHOR_MAX 32u /**< Author field bytes, including its required NUL terminator. */
#define MICROAPP_DESC_MAX 64u /**< Description field bytes, including its required NUL terminator. */
#define MICROAPP_BOARD_ID_MAX 32u /**< Host profile ID field bytes, including its required NUL terminator. */
#define MICROAPP_RESOURCE_NAME_MAX 32u /**< Resource-name field bytes, including its required NUL terminator. */
#define MICROAPP_RESOURCE_TABLE_MAGIC 0x5452414Du /**< Little-endian MART resource-section identifier. */
#define MICROAPP_RESOURCE_TABLE_VERSION 1u /**< Supported resource table version, independent of package version. */
#define MICROAPP_ARCH_ARMV7EM 1u /**< ARMv7E-M Cortex-M4 Thumb target identifier. */
#define MICROAPP_FLOAT_SOFT 0u /**< Software floating-point operations and base AAPCS argument passing. */
#define MICROAPP_FLOAT_SOFTFP 1u /**< Hardware floating-point operations with base AAPCS argument passing. */
#define MICROAPP_FLOAT_HARD 2u /**< Hardware floating point with VFP-register argument passing. */
#define MICROAPP_FIXUP_FLASH 1u /**< Write actual Flash body base plus target_offset into instance RAM. */
#define MICROAPP_FIXUP_RAM 2u /**< Write actual instance RAM base plus target_offset into instance RAM. */


/**
 * @brief Packed 312-byte format-2.0 manifest; integers are little endian and offsets are relative.
 */
typedef struct __attribute__((packed)) {
    char board_id[32]; /**< NUL-terminated host ABI profile ID, not necessarily a board model. */
    char app_id[32]; /**< Unique NUL-terminated App ID. */
    char name[32]; /**< NUL-terminated display name. */
    char version[16]; /**< NUL-terminated App display version. */
    char author[32]; /**< Optional NUL-terminated author. */
    char description[64]; /**< Optional NUL-terminated description. */
    uint32_t abi_major; /**< Required host ABI major. */
    uint32_t abi_minor; /**< Required minimum supported host ABI minor. */
    uint32_t min_host_version; /**< Minimum host version. */
    uint32_t permission_mask; /**< Requested host-defined permission bits. */
    uint32_t architecture; /**< MICROAPP_ARCH_ARMV7EM. */
    uint32_t float_abi; /**< MICROAPP_FLOAT_* convention, must match the host. */
    uint32_t image_size; /**< Code plus read-only bytes, starting at body offset zero. */
    uint32_t image_alignment; /**< Actual body base alignment, power of two from ELF. */
    uint32_t image_crc32; /**< CRC of code/read-only image only. */
    uint32_t entry_offset; /**< Body-relative Thumb entry with low bit retained. */
    uint32_t ram_size; /**< Whole instance RAM bytes including GOT, data, padding and BSS. */
    uint32_t ram_alignment; /**< Instance RAM base alignment, power of two from ELF. */
    uint32_t got_offset; /**< Instance-RAM offset of linker GOT base for r9. */
    uint32_t got_size; /**< GOT byte span. */
    uint32_t init_offset; /**< Body offset of initialization bytes copied to RAM offset zero. */
    uint32_t init_size; /**< Bytes initialized from body, including GOT and data.rel.ro. */
    uint32_t bss_offset; /**< Instance-RAM offset of zero-initialized BSS. */
    uint32_t bss_size; /**< BSS byte count through ram_size. */
    uint32_t data_size; /**< Initialized non-GOT data bytes reported by packer. */
    uint32_t fixup_offset; /**< Body offset of ordered 12-byte RAM corrections. */
    uint32_t fixup_count; /**< Number of pointer corrections, sorted by ram_offset. */
    uint32_t resource_offset; /**< Body offset of resource section. */
    uint32_t resource_size; /**< Whole resource-section bytes. */
    uint32_t resource_crc32; /**< CRC of resource section. */
    uint32_t body_size; /**< Whole immutable body including init/fixups/resources. */
    uint32_t body_crc32; /**< CRC of whole immutable body. */
} microapp_manifest_t;
/**
 * @brief Packed 12-byte RAM pointer correction derived from real ELF relocation records.
 */
typedef struct __attribute__((packed)) {
    uint32_t ram_offset; /**< Aligned RAM location of the 32-bit pointer to rewrite; within init_size. */
    uint32_t target_offset; /**< Offset added to actual Flash/RAM base; valid C one-past values are allowed. */
    uint32_t kind; /**< MICROAPP_FIXUP_FLASH or MICROAPP_FIXUP_RAM; Flash function pointers retain Thumb bit. */
} microapp_fixup_t;
/**
 * @brief Packed 12-byte resource table prefix inside the immutable body.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic; /**< MICROAPP_RESOURCE_TABLE_MAGIC (MART). */
    uint16_t version; /**< Resource table version 1. */
    uint16_t entry_count; /**< Number of 44-byte descriptors. */
    uint32_t table_size; /**< Prefix plus descriptor bytes, excluding resource payload. */
} microapp_resource_table_header_t;
/**
 * @brief Packed 44-byte named resource descriptor; offset is resource-section-relative.
 */
typedef struct __attribute__((packed)) {
    char name[32]; /**< Nonempty NUL-terminated resource name. */
    uint32_t offset; /**< Byte offset relative to resource-section start. */
    uint32_t size; /**< Payload bytes, excluding alignment padding. */
    uint32_t crc32; /**< CRC of this resource payload. */
} microapp_resource_entry_t;

/**
 * @brief Legacy 512-byte format-2.0 single-slot header, accepted only for migration.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic; /**< MICROAPP_SLOT_MAGIC (MASL), legacy container only. */
    uint16_t format_major; /**< Package format major 2. */
    uint16_t format_minor; /**< Package format minor 0. */
    uint32_t header_size; /**< 512-byte legacy header size. */
    uint32_t manifest_size; /**< 312-byte manifest size. */
    uint32_t header_crc32; /**< CRC of whole header with this field zeroed. */
    uint32_t image_offset; /**< Host-chosen body offset relative to legacy slot. */
    uint32_t body_size; /**< Whole immutable body bytes. */
    microapp_manifest_t manifest; /**< Unchanged format-2.0 manifest. */
    uint8_t reserved[MICROAPP_SLOT_HEADER_SIZE - 28u - sizeof(microapp_manifest_t)]; /**< Legacy padding in CRC. */
} microapp_slot_header_t;

/**
 * @brief Packed 56-byte package header; all offsets are package-relative.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic; /**< MICROAPP_PACKAGE_MAGIC (MAPP). */
    uint16_t format_major; /**< Package format major 2; version 1 is rejected. */
    uint16_t format_minor; /**< Package format minor 0. */
    uint32_t header_size; /**< 56-byte package header size. */
    uint32_t total_size; /**< All package bytes including opaque signature. */
    uint32_t manifest_offset; /**< Manifest start, currently byte 56. */
    uint32_t manifest_size; /**< 312-byte manifest size. */
    uint32_t image_offset; /**< Whole body start, currently byte 368. */
    uint32_t image_size; /**< Whole body bytes; unlike manifest.image_size, includes init/fixups/resources. */
    uint32_t resource_offset; /**< Package-relative resource start. */
    uint32_t resource_size; /**< Whole resource-section bytes. */
    uint32_t signature_offset; /**< Signature start, equal to signed_size. */
    uint32_t signature_size; /**< Opaque signature byte count; host verifies policy. */
    uint32_t signed_size; /**< CRC/signed range ending after immutable body. */
    uint32_t crc32; /**< CRC over signed_size with this field (bytes 52..55) zeroed. */
} microapp_package_header_t;
/** @cond INTERNAL_LAYOUT_ASSERTS */
#if defined(__cplusplus)
#define MA_ASSERT(c,m) static_assert(c,m)
#else
#define MA_ASSERT(c,m) _Static_assert(c,m)
#endif
MA_ASSERT(sizeof(microapp_manifest_t)==312u,"manifest wire layout");
MA_ASSERT(sizeof(microapp_package_header_t)==56u,"package wire layout");
MA_ASSERT(sizeof(microapp_slot_header_t)==512u,"slot wire layout");
MA_ASSERT(sizeof(microapp_fixup_t)==12u,"fixup wire layout");
MA_ASSERT(sizeof(microapp_resource_entry_t)==44u,"resource wire layout");
#undef MA_ASSERT
/** @endcond */
#endif
/** @} */
