/** @file microapp_crc32.h @brief Incremental IEEE CRC32 used by native packages. */
#ifndef MICROAPP_CRC32_H
#define MICROAPP_CRC32_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t microapp_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t microapp_crc32(const uint8_t *data, size_t len);
#ifdef __cplusplus
}
#endif
#endif
