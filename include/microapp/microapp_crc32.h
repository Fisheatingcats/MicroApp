/** @file microapp_crc32.h
 * @brief Allocation-free reflected IEEE CRC32, polynomial 0xEDB88320.
 * @defgroup microapp_crc CRC32
 * @{
 */
#ifndef MICROAPP_CRC32_H
#define MICROAPP_CRC32_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** @brief Extend an IEEE CRC32 with another byte range.
 * @param[in] crc Zero for a new stream, or the previous finalized result.
 * @param[in] data At least len readable bytes; NULL only if len is zero.
 * @param[in] len Number of bytes to append.
 * @return Finalized CRC32, usable directly as the next crc argument.
 * @note Initial/final XOR is internal. CRC is not signature authentication.
 */
uint32_t microapp_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
/** @brief Compute a finalized CRC32 for one complete byte range.
 * @param[in] data At least len readable bytes; NULL only for zero length.
 * @param[in] len Number of readable bytes.
 * @return IEEE CRC32, equivalent to microapp_crc32_update(0, data, len).
 */
uint32_t microapp_crc32(const uint8_t *data, size_t len);
#ifdef __cplusplus
}
#endif
/** @} */
#endif
