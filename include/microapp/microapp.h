/**
 * @file microapp.h
 * @brief Portable single-foreground application lifecycle and navigation.
 */
#ifndef MICROAPP_H
#define MICROAPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICROAPP_VERSION_MAJOR 0
#define MICROAPP_VERSION_MINOR 0
#define MICROAPP_VERSION_PATCH 1
#define MICROAPP_VERSION_STRING "0.0.1"
#define MICROAPP_CAPACITY 8u

typedef enum {
    MICROAPP_OK = 0,
    MICROAPP_ERR_PARAM = -1,
    MICROAPP_ERR_NOMEM = -2,
    MICROAPP_ERR_BUSY = -3,
    MICROAPP_ERR_NOT_FOUND = -4,
    MICROAPP_ERR_CREATE = -5
} microapp_status_t;

typedef struct {
    const char *id;
    const char *name;
    const char *version;
    const char *author;
    const char *description;
} microapp_info_t;

typedef struct {
    microapp_status_t (*create)(void *host, void *user, void *view, void *param);
    void (*resume)(void *host, void *user);
    void (*pause)(void *host, void *user);
    void (*destroy)(void *host, void *user);
    void (*tick)(void *host, void *user, uint32_t elapsed_ms);
} microapp_lifecycle_t;

typedef struct {
    const microapp_info_t *info;
    const microapp_lifecycle_t *lifecycle;
    void *user;
} microapp_app_t;

/* All callbacks run synchronously in the caller's context. show_view cannot fail. */
typedef struct {
    void *(*create_view)(void *host);
    void (*delete_view)(void *host, void *view);
    void (*show_view)(void *host, void *view);
    void (*did_launch)(void *host, const char *id);
} microapp_port_t;

/* Zero-initialize before first use. Port, descriptors and metadata are borrowed. */
typedef struct {
    const microapp_port_t *port;
    void *host;
    const microapp_app_t *apps[MICROAPP_CAPACITY];
    size_t count;
    const microapp_app_t *active;
    void *active_view;
    const microapp_app_t *pending;
    const microapp_app_t *history[MICROAPP_CAPACITY];
    size_t history_count;
    bool pending_record_history;
    bool dispatching;
} microapp_t;

microapp_status_t microapp_init(microapp_t *state, const microapp_port_t *port, void *host);
microapp_status_t microapp_deinit(microapp_t *state);
microapp_status_t microapp_register(microapp_t *state, const microapp_app_t *app);
microapp_status_t microapp_unregister(microapp_t *state, const char *id);
const microapp_app_t *microapp_find(const microapp_t *state, const char *id);
microapp_status_t microapp_launch(microapp_t *state, const char *id, void *param);
microapp_status_t microapp_request_launch(microapp_t *state, const char *id);
microapp_status_t microapp_request_home(microapp_t *state);
microapp_status_t microapp_request_back(microapp_t *state);
microapp_status_t microapp_go_home(microapp_t *state);
microapp_status_t microapp_tick(microapp_t *state, uint32_t elapsed_ms);
size_t microapp_count(const microapp_t *state);
const microapp_app_t *microapp_at(const microapp_t *state, size_t index);

#ifdef __cplusplus
}
#endif
#endif
