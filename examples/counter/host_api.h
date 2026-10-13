/** @file host_api.h
 * @brief Example-owned log-only host ABI; independent of GUI, HAL and RTOS.
 * @defgroup microapp_counter Counter example
 * @{
 */
#ifndef COUNTER_HOST_API_H
#define COUNTER_HOST_API_H
#include "microapp/microapp_abi_port.h"
/** @brief Complete example host table; size is checked before dereferencing log. */
typedef struct {
    microapp_abi_header_t header; /**< Example ABI 1.0, including full table size. */
    /** @brief Synchronously log a borrowed message and value through the host.
     * @param[in] message NUL-terminated App-owned text; host must copy if retaining.
     * @param[in] count Value to report.
     */
    void (*log)(const char *message, uint32_t count);
} counter_host_api_t;
/** @brief Increment a value through a cross-source-file call.
 * @param[in] value Current count.
 * @return value plus one, with defined unsigned wraparound.
 */
uint32_t counter_increment(uint32_t value);
/** @brief Read-only pointer aggregate placed in RAM data.rel.ro for fixup auditing. */
typedef struct {
    const char *text; /**< Borrowed Flash string pointer, restored in instance RAM. */
    uint32_t (*fn)(uint32_t); /**< Thumb function pointer, restored in instance RAM. */
} counter_indirect_t;
/** @brief Example aggregate proving both string and function-pointer preparation. */
extern const counter_indirect_t counter_indirect;
#endif
/** @} */
