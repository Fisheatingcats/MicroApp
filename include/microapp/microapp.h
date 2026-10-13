/**
 * @file microapp.h
 * @brief Portable single-foreground application lifecycle and navigation.
 * @details All calls are synchronous and must be serialized by the host. The
 * core borrows descriptors, nodes and callbacks; it does not allocate memory.
 * @defgroup microapp_manager Lifecycle and registry
 * @{
 */
#ifndef MICROAPP_H
#define MICROAPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICROAPP_VERSION_MAJOR 0 /**< Framework major version; independent of host ABI. */
#define MICROAPP_VERSION_MINOR 0 /**< Framework minor version. */
#define MICROAPP_VERSION_PATCH 2 /**< Framework patch version. */
#define MICROAPP_VERSION_STRING "V0.0.2" /**< Human-readable framework version. */
#define MICROAPP_CAPACITY 8u /**< Static registration slots; dynamic nodes do not consume them. */
#define MICROAPP_HISTORY_CAPACITY 8u /**< Maximum saved navigation entries, independent of App count. */

/** @brief In-process manager results; distinct from native ABI and store results. */
typedef enum {
    MICROAPP_OK = 0, /**< Operation completed or current App already selected. */
    MICROAPP_ERR_PARAM = -1, /**< Invalid state, descriptor or argument. */
    MICROAPP_ERR_NOMEM = -2, /**< Static slots exhausted or host view allocation failed. */
    MICROAPP_ERR_BUSY = -3, /**< Duplicate ID/node, active unregister, or reentrant dispatch. */
    MICROAPP_ERR_NOT_FOUND = -4, /**< Requested descriptor or Home is unavailable. */
    MICROAPP_ERR_CREATE = -5 /**< App-specific creation failure convention. */
} microapp_status_t;

/** @brief Borrowed metadata; strings remain valid until successful unregister. */
typedef struct {
    const char *id; /**< Nonempty, NUL-terminated unique ID. */
    const char *name; /**< Host-defined display name; manager does not interpret it. */
    const char *version; /**< Display version, independent of binary ABI. */
    const char *author; /**< Optional author text. */
    const char *description; /**< Optional description text. */
} microapp_info_t;

/**
 * @brief App callbacks, invoked synchronously with the borrowed host and user.
 * @details Only create is mandatory. A failed create must release its own partial
 * resources; destroy is not automatically called for that failed create. Deferred
 * microapp_request_* navigation is allowed inside callbacks; immediate dispatch
 * and registration mutation are rejected while dispatching.
 */
typedef struct {
    /** @brief Initialize one instance in an empty host-owned view.
     * @param[in] host Context supplied to microapp_init().
     * @param[in] user Descriptor-specific context.
     * @param[in] view View created by the host; App must not delete the root.
     * @param[in] param Launch argument, or NULL for deferred/Home navigation.
     * @return MICROAPP_OK on success; other results trigger Home recovery.
     */
    microapp_status_t (*create)(void *host, void *user, void *view, void *param);
    /** @brief Resume a created instance; host/user are borrowed callback contexts. */
    void (*resume)(void *host, void *user);
    /** @brief Pause the active instance before destroy; host/user are borrowed contexts. */
    void (*pause)(void *host, void *user);
    /** @brief Release App resources before the next create; host deletes the view later. */
    void (*destroy)(void *host, void *user);
    /** @brief Receive elapsed milliseconds when no deferred navigation is being committed. */
    void (*tick)(void *host, void *user, uint32_t elapsed_ms);
} microapp_lifecycle_t;

/** @brief Borrowed descriptor binding metadata, lifecycle and per-App state. */
typedef struct {
    const microapp_info_t *info; /**< Required metadata with a valid ID. */
    const microapp_lifecycle_t *lifecycle; /**< Required callback table with create. */
    void *user; /**< Opaque context owned by the host/App. */
} microapp_app_t;

/** @brief Zero-initialized caller-owned intrusive node, detached on unregister/deinit.
 * @note Keep this node alive while registered. Core never frees it. There is no
 * fixed dynamic-App count; actual memory/capacity limits belong to the host.
 */
typedef struct microapp_registration {
    const microapp_app_t *app; /**< Borrowed descriptor; NULL when detached. */
    struct microapp_registration *next; /**< Core-managed link; NULL when detached. */
} microapp_registration_t;

/** @brief Host view adapter; callbacks execute in the caller's context.
 * @note create_view/delete_view/show_view are mandatory; show_view cannot fail.
 */
typedef struct {
    /** @brief Allocate an empty view; return NULL on failure without changing the active view. */
    void *(*create_view)(void *host);
    /** @brief Delete a host-owned root and remaining child objects. */
    void (*delete_view)(void *host, void *view);
    /** @brief Make view visible; this operation must succeed. */
    void (*show_view)(void *host, void *view);
    /** @brief Optional successful-launch notification with the borrowed App ID. */
    void (*did_launch)(void *host, const char *id);
} microapp_port_t;

/** @brief Caller-owned manager state; zero-initialize before first use.
 * @warning Members are observable state, not a mutation API. The host serializes
 * calls and retains all borrowed objects until unregister/deinit succeeds.
 */
typedef struct {
    const microapp_port_t *port; /**< Borrowed view callbacks; NULL after deinit. */
    void *host; /**< Opaque shared host context. */
    const microapp_app_t *apps[MICROAPP_CAPACITY]; /**< Static descriptors in registration order. */
    size_t static_count; /**< Used static slots. */
    microapp_registration_t *dynamic; /**< Head of the caller-owned dynamic list. */
    size_t count; /**< Total static plus dynamic descriptors. */
    const microapp_app_t *active; /**< Sole active descriptor, or NULL during transition/failure. */
    void *active_view; /**< Root currently owned by the manager. */
    const microapp_app_t *pending; /**< Last deferred target, or NULL. */
    const microapp_app_t *history[MICROAPP_HISTORY_CAPACITY]; /**< Borrowed previous descriptors. */
    size_t history_count; /**< Number of saved history entries. */
    bool pending_record_history; /**< Whether committing pending adds the old App to history. */
    bool dispatching; /**< Synchronous reentry guard, not a thread lock. */
} microapp_t;

/** @brief Initialize/reset an inactive manager and detach existing dynamic nodes.
 * @param[in,out] state Zero-initialized or previously initialized inactive state.
 * @param[in] port Borrowed mandatory view callbacks.
 * @param[in] host Opaque context, allowed to be NULL.
 * @retval MICROAPP_OK State initialized; previous registrations cleared.
 * @retval MICROAPP_ERR_PARAM Invalid state/port or missing mandatory callback.
 * @retval MICROAPP_ERR_BUSY An instance is active or a callback is executing.
 */
microapp_status_t microapp_init(microapp_t *state, const microapp_port_t *port, void *host);
/** @brief Destroy the active instance/view, detach nodes and clear all state.
 * @param[in,out] state Initialized manager.
 * @return MICROAPP_OK, MICROAPP_ERR_PARAM, or MICROAPP_ERR_BUSY during dispatch.
 * @note Nodes and descriptors remain caller-owned and are never freed here.
 */
microapp_status_t microapp_deinit(microapp_t *state);
/** @brief Borrow a descriptor into a static slot; register Home first.
 * @param[in,out] state Initialized manager.
 * @param[in] app Descriptor retained by the caller until unregister/deinit.
 * @return MICROAPP_OK; PARAM for invalid descriptor; BUSY for duplicate/reentry;
 * NOMEM when all MICROAPP_CAPACITY static slots are used.
 */
microapp_status_t microapp_register(microapp_t *state, const microapp_app_t *app);
/** @brief Append a caller-owned node without consuming a static slot.
 * @param[in,out] state Initialized manager.
 * @param[in,out] registration Zero-initialized detached node with registration lifetime.
 * @param[in] app Borrowed descriptor with a unique ID.
 * @return MICROAPP_OK, MICROAPP_ERR_PARAM, or MICROAPP_ERR_BUSY for a used node,
 * duplicate ID or dispatch in progress.
 */
microapp_status_t microapp_register_dynamic(microapp_t *state,
    microapp_registration_t *registration, const microapp_app_t *app);
/** @brief Remove an inactive descriptor and its pending/history references.
 * @param[in,out] state Initialized manager.
 * @param[in] id NUL-terminated ID.
 * @return MICROAPP_OK, PARAM, NOT_FOUND, or BUSY for active App/dispatch.
 * @note This does not uninstall Flash data. Detached dynamic node fields are cleared.
 */
microapp_status_t microapp_unregister(microapp_t *state, const char *id);
/** @brief Find a borrowed descriptor, searching static slots then dynamic nodes.
 * @param[in] state Manager to search.
 * @param[in] id NUL-terminated ID.
 * @return Borrowed descriptor, or NULL for invalid state/ID or no match.
 */
const microapp_app_t *microapp_find(const microapp_t *state, const char *id);
/** @brief Immediately switch to an App, destroying the old instance before create.
 * @param[in,out] state Initialized manager.
 * @param[in] id Target ID.
 * @param[in] param Argument passed only to this target's create callback.
 * @return OK (including same active target), NOT_FOUND, BUSY, NOMEM, or create's error.
 * @details Empty-view allocation failure preserves the old instance. A failed
 * create cleans its view and attempts to recreate the first registered App;
 * successful fallback does not replace the original error result. History is
 * bounded and cleared on failed create. No instance rollback is provided.
 */
microapp_status_t microapp_launch(microapp_t *state, const char *id, void *param);
/** @brief Queue a launch for the next tick; the latest request replaces pending.
 * @param[in,out] state Initialized manager; safe from synchronous App callbacks.
 * @param[in] id Target ID.
 * @return OK if queued, NOT_FOUND for invalid state/ID or absent target.
 * @note Successful queuing does not mean CREATE has run. Deferred param is NULL.
 */
microapp_status_t microapp_request_launch(microapp_t *state, const char *id);
/** @brief Clear history and queue the first registered App as Home.
 * @param[in,out] state Initialized manager.
 * @return OK if queued, NOT_FOUND if the manager/Home is unavailable.
 */
microapp_status_t microapp_request_home(microapp_t *state);
/** @brief Pop history and queue Back, falling back to Home if history is empty.
 * @param[in,out] state Initialized manager.
 * @return OK, PARAM for uninitialized state, or NOT_FOUND when Home is unavailable.
 * @note History is popped when requested, before the deferred switch succeeds.
 */
microapp_status_t microapp_request_back(microapp_t *state);
/** @brief Clear history/pending and immediately switch to the first registered App.
 * @param[in,out] state Initialized manager.
 * @return Same switch results as microapp_launch(); NOT_FOUND if no Home exists.
 * @note Inside callbacks prefer microapp_request_home().
 */
microapp_status_t microapp_go_home(microapp_t *state);
/** @brief Commit one pending navigation, otherwise dispatch the active App tick.
 * @param[in,out] state Initialized manager.
 * @param[in] elapsed_ms Elapsed time supplied by the host; core owns no clock/task.
 * @return OK, PARAM, BUSY, or the deferred launch result.
 * @note A tick that commits navigation does not also invoke the App tick callback.
 */
microapp_status_t microapp_tick(microapp_t *state, uint32_t elapsed_ms);
/** @brief Query total static plus dynamic registrations.
 * @param[in] state Manager, allowed to be NULL.
 * @return Total descriptor count, or zero for NULL.
 */
size_t microapp_count(const microapp_t *state);
/** @brief Enumerate static registration order followed by dynamic list order.
 * @param[in] state Manager.
 * @param[in] index Zero-based enumeration index.
 * @return Borrowed descriptor, or NULL for invalid state/out-of-range index.
 */
const microapp_app_t *microapp_at(const microapp_t *state, size_t index);

#ifdef __cplusplus
}
#endif
/** @} */
#endif
