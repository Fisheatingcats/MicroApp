/**
 * @brief Static/dynamic registry and single-instance deferred navigation.
 * @file microapp.c
 */
#include "microapp/microapp.h"
#include <string.h>

/** @brief Initialize/reset an inactive manager and detach existing dynamic nodes.
 * @see microapp_init
 */
microapp_status_t microapp_init(microapp_t *state, const microapp_port_t *port, void *host)
{
    if (!state || !port || !port->create_view || !port->delete_view || !port->show_view) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->active || state->dispatching) {
        return MICROAPP_ERR_BUSY;
    }
    for (microapp_registration_t *node = state->dynamic; node;) {
        microapp_registration_t *next = node->next;
        node->app = NULL;
        node->next = NULL;
        node = next;
    }
    memset(state, 0, sizeof(*state));
    state->port = port;
    state->host = host;
    return MICROAPP_OK;
}

/** @brief Destroy the active instance/view, detach nodes and clear all state.
 * @see microapp_deinit
 */
microapp_status_t microapp_deinit(microapp_t *state)
{
    if (!state || !state->port) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->dispatching) {
        return MICROAPP_ERR_BUSY;
    }
    state->dispatching = true;
    if (state->active && state->active->lifecycle->pause) {
        state->active->lifecycle->pause(state->host, state->active->user);
    }
    if (state->active && state->active->lifecycle->destroy) {
        state->active->lifecycle->destroy(state->host, state->active->user);
    }
    if (state->active_view) {
        state->port->delete_view(state->host, state->active_view);
    }
    for (microapp_registration_t *node = state->dynamic; node;) {
        microapp_registration_t *next = node->next;
        node->app = NULL;
        node->next = NULL;
        node = next;
    }
    memset(state, 0, sizeof(*state));
    return MICROAPP_OK;
}

/** @brief Find a borrowed descriptor, searching static slots then dynamic nodes.
 * @see microapp_find
 */
const microapp_app_t *microapp_find(const microapp_t *state, const char *id)
{
    if (!state || !state->port || !id) {
        return NULL;
    }
    for (size_t i = 0u; i < state->static_count; ++i) {
        if (strcmp(state->apps[i]->info->id, id) == 0) {
            return state->apps[i];
        }
    }
    for (const microapp_registration_t *node = state->dynamic; node; node = node->next) {
        if (strcmp(node->app->info->id, id) == 0) return node->app;
    }
    return NULL;
}

/** @brief Borrow a descriptor into a static slot; register Home first.
 * @see microapp_register
 */
microapp_status_t microapp_register(microapp_t *state, const microapp_app_t *app)
{
    if (!state || !state->port || !app || !app->info || !app->info->id ||
        app->info->id[0] == '\0' || !app->lifecycle || !app->lifecycle->create) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->dispatching || microapp_find(state, app->info->id)) {
        return MICROAPP_ERR_BUSY;
    }
    if (state->static_count == MICROAPP_CAPACITY) {
        return MICROAPP_ERR_NOMEM;
    }
    state->apps[state->static_count++] = app;
    ++state->count;
    return MICROAPP_OK;
}

/** @brief Append a caller-owned node without consuming a static slot.
 * @see microapp_register_dynamic
 */
microapp_status_t microapp_register_dynamic(microapp_t *state,
    microapp_registration_t *registration, const microapp_app_t *app)
{
    if (!state || !state->port || !registration || !app || !app->info ||
        !app->info->id || !app->info->id[0] || !app->lifecycle || !app->lifecycle->create)
        return MICROAPP_ERR_PARAM;
    if (registration->app || registration->next) return MICROAPP_ERR_BUSY;
    if (state->dispatching || microapp_find(state, app->info->id)) return MICROAPP_ERR_BUSY;
    microapp_registration_t **tail = &state->dynamic;
    while (*tail) {
        if (*tail == registration) return MICROAPP_ERR_BUSY;
        tail = &(*tail)->next;
    }
    registration->app = app;
    registration->next = NULL;
    *tail = registration;
    ++state->count;
    return MICROAPP_OK;
}

/** @brief Remove an inactive descriptor and its pending/history references.
 * @see microapp_unregister
 */
microapp_status_t microapp_unregister(microapp_t *state, const char *id)
{
    const microapp_app_t *app;
    size_t dst = 0u;

    if (!state || !state->port || !id) {
        return MICROAPP_ERR_PARAM;
    }
    app = microapp_find(state, id);
    if (!app) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    if (state->dispatching || state->active == app) {
        return MICROAPP_ERR_BUSY;
    }
    for (size_t i = 0u; i < state->static_count; ++i) {
        if (state->apps[i] != app) {
            state->apps[dst++] = state->apps[i];
        }
    }
    if (dst == state->static_count) {
        microapp_registration_t **link = &state->dynamic;
        while (*link && (*link)->app != app) link = &(*link)->next;
        if (*link) {
            microapp_registration_t *node = *link;
            *link = node->next;
            node->app = NULL;
            node->next = NULL;
        }
    }
    state->static_count = dst;
    --state->count;
    if (dst < MICROAPP_CAPACITY) state->apps[dst] = NULL;
    dst = 0u;
    for (size_t i = 0u; i < state->history_count; ++i) {
        if (state->history[i] != app) {
            state->history[dst++] = state->history[i];
        }
    }
    state->history_count = dst;
    if (state->pending == app) {
        state->pending = NULL;
    }
    return MICROAPP_OK;
}

/**
 * @brief Switch instances without overlapping their resource lifetimes.
 * @param state Manager state.
 * @param next Borrowed target descriptor.
 * @param param Target create payload.
 * @param record_history Whether to save the old descriptor on success.
 * @return A switch result or the target create error.
 * @details Allocate an empty view before destroying old; after failed CREATE, retry Home, not the destroyed old instance.
 */
static microapp_status_t launch(microapp_t *state, const microapp_app_t *next,
                                void *param, bool record_history)
{
    const microapp_app_t *old;
    void *old_view;
    void *view;
    microapp_status_t rc;

    if (!state || !state->port || !next) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    if (state->dispatching) {
        return MICROAPP_ERR_BUSY;
    }
    /* One instance per descriptor: never create it over its existing state. */
    if (state->active == next) {
        return MICROAPP_OK;
    }
    state->dispatching = true;
    view = state->port->create_view(state->host);
    if (!view) {
        state->dispatching = false;
        return MICROAPP_ERR_NOMEM;
    }
    old = state->active;
    old_view = state->active_view;
    if (old && old->lifecycle->pause) {
        old->lifecycle->pause(state->host, old->user);
    }
    if (old && old->lifecycle->destroy) {
        old->lifecycle->destroy(state->host, old->user);
    }
    state->active = NULL;
    state->active_view = NULL;
    /* Shared App RAM must be released before the next CREATE can touch it. */
    rc = next->lifecycle->create(state->host, next->user, view, param);
    if (rc != MICROAPP_OK) {
        state->port->delete_view(state->host, view);
        view = state->port->create_view(state->host);
        const microapp_app_t *home = microapp_at(state, 0u);
        state->history_count = 0u;
        state->pending = NULL;
        if (view) {
            if (home && home != next && home->lifecycle->create(state->host, home->user, view, NULL) == MICROAPP_OK) {
                state->active = home;
                if (home->lifecycle->resume) home->lifecycle->resume(state->host, home->user);
            }
            state->active_view = view;
            state->port->show_view(state->host, view);
            if (state->active && state->port->did_launch)
                state->port->did_launch(state->host, state->active->info->id);
        } else if (old_view) {
            /* Keep the empty previous view owned until recovery/deinit. */
            state->active_view = old_view;
            old_view = NULL;
        }
        if (old_view) state->port->delete_view(state->host, old_view);
        state->dispatching = false;
        return rc;
    }
    if (record_history && old && state->history_count < MICROAPP_HISTORY_CAPACITY)
        state->history[state->history_count++] = old;
    state->active = next;
    state->active_view = view;
    state->port->show_view(state->host, view);
    if (old_view) {
        state->port->delete_view(state->host, old_view);
    }
    if (next->lifecycle->resume) {
        next->lifecycle->resume(state->host, next->user);
    }
    if (state->port->did_launch) {
        state->port->did_launch(state->host, next->info->id);
    }
    state->dispatching = false;
    return MICROAPP_OK;
}

/** @brief Immediately switch to an App, destroying the old instance before create.
 * @see microapp_launch
 */
microapp_status_t microapp_launch(microapp_t *state, const char *id, void *param)
{
    return launch(state, microapp_find(state, id), param, true);
}

/** @brief Queue a launch for the next tick; the latest request replaces pending.
 * @see microapp_request_launch
 */
microapp_status_t microapp_request_launch(microapp_t *state, const char *id)
{
    const microapp_app_t *app = microapp_find(state, id);

    if (!app) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    state->pending = app;
    state->pending_record_history = true;
    return MICROAPP_OK;
}

/** @brief Clear history and queue the first registered App as Home.
 * @see microapp_request_home
 */
microapp_status_t microapp_request_home(microapp_t *state)
{
    if (!state || !state->port || state->count == 0u) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    state->history_count = 0u;
    state->pending = microapp_at(state, 0u);
    state->pending_record_history = false;
    return MICROAPP_OK;
}

/** @brief Pop history and queue Back, falling back to Home if history is empty.
 * @see microapp_request_back
 */
microapp_status_t microapp_request_back(microapp_t *state)
{
    if (!state || !state->port) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->history_count > 0u) {
        state->pending = state->history[--state->history_count];
        state->pending_record_history = false;
        return MICROAPP_OK;
    }
    return microapp_request_home(state);
}

/** @brief Clear history/pending and immediately switch to the first registered App.
 * @see microapp_go_home
 */
microapp_status_t microapp_go_home(microapp_t *state)
{
    if (!state || !state->port || state->count == 0u) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    state->history_count = 0u;
    state->pending = NULL;
    return launch(state, microapp_at(state, 0u), NULL, false);
}

/** @brief Commit one pending navigation, otherwise dispatch the active App tick.
 * @see microapp_tick
 */
microapp_status_t microapp_tick(microapp_t *state, uint32_t elapsed_ms)
{
    if (!state || !state->port) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->dispatching) {
        return MICROAPP_ERR_BUSY;
    }
    if (state->pending) {
        const microapp_app_t *next = state->pending;
        bool record = state->pending_record_history;

        state->pending = NULL;
        return launch(state, next, NULL, record);
    }
    if (state->active && state->active->lifecycle->tick) {
        state->dispatching = true;
        state->active->lifecycle->tick(state->host, state->active->user, elapsed_ms);
        state->dispatching = false;
    }
    return MICROAPP_OK;
}

/** @brief Query total static plus dynamic registrations.
 * @see microapp_count
 */
size_t microapp_count(const microapp_t *state)
{
    return state ? state->count : 0u;
}

/** @brief Enumerate static registration order followed by dynamic list order.
 * @see microapp_at
 */
const microapp_app_t *microapp_at(const microapp_t *state, size_t index)
{
    if (!state || index >= state->count) return NULL;
    if (index < state->static_count) return state->apps[index];
    index -= state->static_count;
    const microapp_registration_t *node = state->dynamic;
    while (node && index--) node = node->next;
    return node ? node->app : NULL;
}
