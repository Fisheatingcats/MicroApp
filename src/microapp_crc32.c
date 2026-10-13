/** @file microapp_crc32.c @brief Allocation-free IEEE CRC32. */
#include "microapp/microapp_crc32.h"

/** @brief Extend an IEEE CRC32 with another byte range.
 * @see microapp_crc32_update
 */
uint32_t microapp_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/** @brief Compute a finalized CRC32 for one complete byte range.
 * @see microapp_crc32
 */
uint32_t microapp_crc32(const uint8_t *data, size_t len)
{
    return microapp_crc32_update(0u, data, len);
}
