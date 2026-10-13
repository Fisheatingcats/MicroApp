/** @file microapp_abi_port.h
 * @brief Native in-memory boundary; full service tables are host-defined.
 * @defgroup microapp_abi Native host ABI
 * @{
 */
#ifndef MICROAPP_ABI_PORT_H
#define MICROAPP_ABI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Native service results, distinct from in-process manager/store statuses. */
typedef enum {
    MICROAPP_ABI_OK = 0, /**< Service operation completed. */
    MICROAPP_ABI_ERR_PARAM = -1, /**< Invalid service argument. */
    MICROAPP_ABI_ERR_DENIED = -2, /**< Host policy denied the operation. */
    MICROAPP_ABI_ERR_UNSUPPORTED = -3, /**< Service/ABI operation unavailable. */
    MICROAPP_ABI_ERR_IO = -4, /**< Host I/O failure. */
    MICROAPP_ABI_ERR_NOT_FOUND = -5 /**< Host object/resource absent. */
} microapp_abi_status_t;

/** @brief Stable reason numbers passed to the canonical native entry. */
typedef enum {
    MICROAPP_ABI_REASON_CREATE = 1, /**< Initialize one prepared instance. */
    MICROAPP_ABI_REASON_RESUME = 2, /**< Activate a created instance. */
    MICROAPP_ABI_REASON_PAUSE = 3, /**< Pause before destroy. */
    MICROAPP_ABI_REASON_TICK = 4, /**< Host-defined elapsed-time payload. */
    MICROAPP_ABI_REASON_DESTROY = 5 /**< Release App resources and callbacks. */
} microapp_abi_reason_t;

/** @brief Host-retained per-instance context, not persistent disk metadata.
 * @details Permission bit meanings and host_user ownership are defined by the host.
 * The host retains the context for the lifetime of the native App, including
 * delayed callbacks. This struct is an in-memory ABI, not disk metadata.
 */
typedef struct microapp_abi_context_s {
    uint32_t permissions; /**< Granted host-defined permission bits. */
    void *host_user; /**< Borrowed host-owned instance context. */
} microapp_abi_context_t;

/** @brief Common prefix of the host-defined typed service table.
 * @details Host tables begin with these fields, followed by host-specific service APIs.
 * The host defines the complete typed table and native entry signature.
 * It owns ABI versioning; framework versions do not select a host ABI.
 */
typedef struct {
    uint32_t abi_major; /**< Breaking interface version, checked for equality. */
    uint32_t abi_minor; /**< Backward-compatible interface additions. */
    uint32_t struct_size; /**< Complete host table bytes, checked before field access. */
} microapp_abi_header_t;

/** @brief Canonical signature; App exports a function named microapp_main.
 * @param[in] host_api Borrowed host-defined table beginning with microapp_abi_header_t.
 * @param[in,out] context Host-retained instance context and granted permissions.
 * @param[in] reason One of microapp_abi_reason_t.
 * @param[in] arg Host-defined reason payload, possibly NULL.
 * @return Host-defined signed lifecycle result; zero denotes success.
 * @note Host owns table/versioning. Every native entry and delayed App callback
 * must use the instance r9 bridge. This ABI supplies no GUI or device service table.
 */
typedef int32_t (*microapp_main_fn)(const void *host_api,
                                  microapp_abi_context_t *context,
                                  uint32_t reason, void *arg);

#ifdef __cplusplus
}
#endif
/** @} */
#endif
