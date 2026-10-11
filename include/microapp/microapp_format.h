/**
 * @file microapp_format.h
 * @brief Native package format 1.0 and installed slot metadata.
 */
#ifndef MICROAPP_FORMAT_H
#define MICROAPP_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICROAPP_PACKAGE_MAGIC 0x504B5746u /* FWKP, little-endian */
#define MICROAPP_SLOT_MAGIC    0x4C535746u /* FWSL, little-endian */
#define MICROAPP_FORMAT_MAJOR  1u
#define MICROAPP_FORMAT_MINOR  0u

#define MICROAPP_SLOT_HEADER_SIZE 512u

#define MICROAPP_ID_MAX        32u
#define MICROAPP_NAME_MAX      32u
#define MICROAPP_VERSION_MAX   16u
#define MICROAPP_AUTHOR_MAX    32u
#define MICROAPP_DESC_MAX      64u
#define MICROAPP_BOARD_ID_MAX  32u
#define MICROAPP_RESOURCE_NAME_MAX 32u
#define MICROAPP_RESOURCE_TABLE_MAGIC 0x54525746u /* FWRT, little-endian */
#define MICROAPP_RESOURCE_TABLE_VERSION 1u

typedef struct __attribute__((packed)) {
    char board_id[MICROAPP_BOARD_ID_MAX];
    char app_id[MICROAPP_ID_MAX];
    char name[MICROAPP_NAME_MAX];
    char version[MICROAPP_VERSION_MAX];
    char author[MICROAPP_AUTHOR_MAX];
    char description[MICROAPP_DESC_MAX];
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
} microapp_manifest_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint32_t table_size;
} microapp_resource_table_header_t;

typedef struct __attribute__((packed)) {
    char name[MICROAPP_RESOURCE_NAME_MAX];
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
} microapp_resource_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t format_major;
    uint16_t format_minor;
    uint32_t header_size;
    uint32_t manifest_size;
    uint32_t header_crc32;
    microapp_manifest_t manifest;
    uint8_t reserved[MICROAPP_SLOT_HEADER_SIZE -
                     (sizeof(uint32_t) * 4u) -
                     (sizeof(uint16_t) * 2u) -
                     sizeof(microapp_manifest_t)];
} microapp_slot_header_t;

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
} microapp_package_header_t;

#if defined(__cplusplus)
#define MICROAPP_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define MICROAPP_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

MICROAPP_STATIC_ASSERT(sizeof(microapp_manifest_t) == 280u,
                    "microapp_manifest_t layout changed");
MICROAPP_STATIC_ASSERT(sizeof(microapp_slot_header_t) ==
                        MICROAPP_SLOT_HEADER_SIZE,
                    "microapp_slot_header_t must match slot header size");
MICROAPP_STATIC_ASSERT(sizeof(microapp_package_header_t) == 56u,
                    "microapp_package_header_t layout changed");
MICROAPP_STATIC_ASSERT(sizeof(microapp_resource_table_header_t) == 12u,
                    "microapp_resource_table_header_t layout changed");
MICROAPP_STATIC_ASSERT(sizeof(microapp_resource_entry_t) == 44u,
                    "microapp_resource_entry_t layout changed");

#undef MICROAPP_STATIC_ASSERT

#ifdef __cplusplus
}
#endif

#endif /* MICROAPP_FORMAT_H */
