/** @file counter.c
 * @brief Freestanding App exercising GOT, initialized data, BSS and indirect references.
 */
#include "host_api.h"
/** @brief Initialized counter restored to seven on each new instance. */
static uint32_t count = 7u;
/** @brief Zero-initialized tick counter. */
static uint32_t ticks;
/** @brief Initialized RAM pointer to an immutable Flash message. */
static const char *volatile message = "MicroApp counter";
/** @brief Initialized RAM Thumb pointer to a cross-file helper. */
static uint32_t (*volatile increment)(uint32_t) = counter_increment;
/** @brief Exported native entry, receiving only injected log services.
 * @param[in] host_api counter_host_api_t with ABI major 1 and a valid log callback.
 * @param[in,out] ctx Host-retained context, unused by this example.
 * @param[in] reason CREATE logs 7 and 8; first TICK logs 9, later values keep advancing.
 * @param[in] arg Host payload, unused here.
 * @return Zero on accepted ABI, or -1 for a missing/incompatible service table.
 * @note RAM has already been prepared by the host. Entry does not initialize r9.
 */
int32_t microapp_main(const void *host_api, microapp_abi_context_t *ctx, uint32_t reason, void *arg)
{
    const counter_host_api_t *api=host_api;
    (void)ctx; (void)arg;
    if(!api || api->header.abi_major!=1u || api->header.struct_size<sizeof(*api) || !api->log) return -1;
    if(reason==MICROAPP_ABI_REASON_CREATE) {
        api->log(message,count);
        api->log(counter_indirect.text,counter_indirect.fn(count));
    } else if(reason==MICROAPP_ABI_REASON_TICK) {
        count=increment(count); ticks++; api->log(message,count+ticks);
    }
    return 0;
}
