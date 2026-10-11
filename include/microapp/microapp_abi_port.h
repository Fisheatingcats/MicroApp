/** @file microapp_abi_port.h @brief Native App boundary; service tables are host-defined. */
#ifndef MICROAPP_ABI_PORT_H
#define MICROAPP_ABI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native ABI results are distinct from the in-process manager's status enum. */
typedef enum {
    MICROAPP_ABI_OK = 0,
    MICROAPP_ABI_ERR_PARAM = -1,
    MICROAPP_ABI_ERR_DENIED = -2,
    MICROAPP_ABI_ERR_UNSUPPORTED = -3,
    MICROAPP_ABI_ERR_IO = -4,
    MICROAPP_ABI_ERR_NOT_FOUND = -5
} microapp_abi_status_t;

typedef enum {
    MICROAPP_ABI_REASON_CREATE = 1,
    MICROAPP_ABI_REASON_RESUME = 2,
    MICROAPP_ABI_REASON_PAUSE = 3,
    MICROAPP_ABI_REASON_TICK = 4,
    MICROAPP_ABI_REASON_DESTROY = 5
} microapp_abi_reason_t;

/* Permission bit meanings and host_user ownership are defined by the host.
 * The host retains the context for the lifetime of the native App, including
 * delayed callbacks. This struct is an in-memory ABI, not disk metadata.
 */
typedef struct microapp_abi_context_s {
    uint32_t permissions;
    void *host_user;
} microapp_abi_context_t;

/* Host tables begin with these fields, followed by host-specific service APIs.
 * The host defines the complete typed table and native entry signature.
 * It owns ABI versioning; framework versions do not select a host ABI.
 */
typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
} microapp_abi_header_t;

#ifdef __cplusplus
}
#endif
#endif
