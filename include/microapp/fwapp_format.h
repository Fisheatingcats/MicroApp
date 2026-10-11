/**
 * @file fwapp_format.h
 * @brief On-disk .fwapp and installed slot metadata.
 */
#ifndef FWAPP_FORMAT_H
#define FWAPP_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FWAPP_PACKAGE_MAGIC 0x504B5746u /* FWKP, little-endian */
#define FWAPP_SLOT_MAGIC    0x4C535746u /* FWSL, little-endian */
#define FWAPP_FORMAT_MAJOR  1u
#define FWAPP_FORMAT_MINOR  0u

#define FWAPP_SLOT_HEADER_SIZE 512u

#define FWAPP_ID_MAX        32u
#define FWAPP_NAME_MAX      32u
#define FWAPP_VERSION_MAX   16u
#define FWAPP_AUTHOR_MAX    32u
#define FWAPP_DESC_MAX      64u
#define FWAPP_BOARD_ID_MAX  32u
#define FWAPP_RESOURCE_NAME_MAX 32u
#define FWAPP_RESOURCE_TABLE_MAGIC 0x54525746u /* FWRT, little-endian */
#define FWAPP_RESOURCE_TABLE_VERSION 1u

typedef struct __attribute__((packed)) {
    char board_id[FWAPP_BOARD_ID_MAX];
    char app_id[FWAPP_ID_MAX];
    char name[FWAPP_NAME_MAX];
    char version[FWAPP_VERSION_MAX];
    char author[FWAPP_AUTHOR_MAX];
    char description[FWAPP_DESC_MAX];
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t min_host_version;
    uint32_t permission_mask;
    uint32_t slot_base;
    uint32_t slot_size;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t entry_offset;
    uint32_t ram_base;
    uint32_t ram_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t resource_offset;
    uint32_t resource_size;
    uint32_t resource_crc32;
} fwapp_manifest_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint32_t table_size;
} fwapp_resource_table_header_t;

typedef struct __attribute__((packed)) {
    char name[FWAPP_RESOURCE_NAME_MAX];
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
} fwapp_resource_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t format_major;
    uint16_t format_minor;
    uint32_t header_size;
    uint32_t manifest_size;
    uint32_t header_crc32;
    fwapp_manifest_t manifest;
    uint8_t reserved[FWAPP_SLOT_HEADER_SIZE -
                     (sizeof(uint32_t) * 4u) -
                     (sizeof(uint16_t) * 2u) -
                     sizeof(fwapp_manifest_t)];
} fwapp_slot_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t format_major;
    uint16_t format_minor;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t manifest_offset;
    uint32_t manifest_size;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t resource_offset;
    uint32_t resource_size;
    uint32_t signature_offset;
    uint32_t signature_size;
    uint32_t signed_size;
    uint32_t crc32;
} fwapp_package_header_t;

#if defined(__cplusplus)
#define FWAPP_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define FWAPP_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

FWAPP_STATIC_ASSERT(sizeof(fwapp_manifest_t) == 280u,
                    "fwapp_manifest_t layout changed");
FWAPP_STATIC_ASSERT(sizeof(fwapp_slot_header_t) ==
                        FWAPP_SLOT_HEADER_SIZE,
                    "fwapp_slot_header_t must match slot header size");
FWAPP_STATIC_ASSERT(sizeof(fwapp_package_header_t) == 56u,
                    "fwapp_package_header_t layout changed");
FWAPP_STATIC_ASSERT(sizeof(fwapp_resource_table_header_t) == 12u,
                    "fwapp_resource_table_header_t layout changed");
FWAPP_STATIC_ASSERT(sizeof(fwapp_resource_entry_t) == 44u,
                    "fwapp_resource_entry_t layout changed");

#undef FWAPP_STATIC_ASSERT

#ifdef __cplusplus
}
#endif

#endif /* FWAPP_FORMAT_H */
