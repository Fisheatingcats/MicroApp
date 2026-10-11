/**
 * @file microapp.c
 * @brief Registry, transactional screen switching and deferred navigation.
 */
#include "microapp/microapp.h"
#include <string.h>

microapp_status_t microapp_init(microapp_t *state, const microapp_port_t *port, void *host)
{
    if (!state || !port || !port->create_view || !port->delete_view || !port->show_view) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->active || state->dispatching) {
        return MICROAPP_ERR_BUSY;
    }
    memset(state, 0, sizeof(*state));
    state->port = port;
    state->host = host;
    return MICROAPP_OK;
}

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
    memset(state, 0, sizeof(*state));
    return MICROAPP_OK;
}

const microapp_app_t *microapp_find(const microapp_t *state, const char *id)
{
    if (!state || !state->port || !id) {
        return NULL;
    }
    for (size_t i = 0u; i < state->count; ++i) {
        if (strcmp(state->apps[i]->info->id, id) == 0) {
            return state->apps[i];
        }
    }
    return NULL;
}

microapp_status_t microapp_register(microapp_t *state, const microapp_app_t *app)
{
    if (!state || !state->port || !app || !app->info || !app->info->id ||
        app->info->id[0] == '\0' || !app->lifecycle || !app->lifecycle->create) {
        return MICROAPP_ERR_PARAM;
    }
    if (state->dispatching || microapp_find(state, app->info->id)) {
        return MICROAPP_ERR_BUSY;
    }
    if (state->count == MICROAPP_CAPACITY) {
        return MICROAPP_ERR_NOMEM;
    }
    state->apps[state->count++] = app;
    return MICROAPP_OK;
}

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
    for (size_t i = 0u; i < state->count; ++i) {
        if (state->apps[i] != app) {
            state->apps[dst++] = state->apps[i];
        }
    }
    state->count = dst;
    state->apps[dst] = NULL;
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
    rc = next->lifecycle->create(state->host, next->user, view, param);
    if (rc != MICROAPP_OK) {
        state->port->delete_view(state->host, view);
        state->dispatching = false;
        return rc;
    }
    old = state->active;
    old_view = state->active_view;
    /* Commit history only after successful creation; Back does not push history. */
    if (record_history && old && state->history_count < MICROAPP_CAPACITY) {
        state->history[state->history_count++] = old;
    }
    if (old && old->lifecycle->pause) {
        old->lifecycle->pause(state->host, old->user);
    }
    if (old && old->lifecycle->destroy) {
        old->lifecycle->destroy(state->host, old->user);
    }
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

microapp_status_t microapp_launch(microapp_t *state, const char *id, void *param)
{
    return launch(state, microapp_find(state, id), param, true);
}

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

microapp_status_t microapp_request_home(microapp_t *state)
{
    if (!state || !state->port || state->count == 0u) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    state->history_count = 0u;
    state->pending = state->apps[0];
    state->pending_record_history = false;
    return MICROAPP_OK;
}

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

microapp_status_t microapp_go_home(microapp_t *state)
{
    if (!state || !state->port || state->count == 0u) {
        return MICROAPP_ERR_NOT_FOUND;
    }
    state->history_count = 0u;
    state->pending = NULL;
    return launch(state, state->apps[0], NULL, false);
}

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

size_t microapp_count(const microapp_t *state)
{
    return state ? state->count : 0u;
}

const microapp_app_t *microapp_at(const microapp_t *state, size_t index)
{
    return state && index < state->count ? state->apps[index] : NULL;
}
