/** @file test_microapp.c @brief Host tests for lifecycle ownership and navigation. */
#include "microapp/microapp.h"
#include "microapp/microapp_crc32.h"
#include "microapp/microapp_format.h"
#include "microapp/microapp_abi_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

typedef struct {
    unsigned creates;
    unsigned resumes;
    unsigned pauses;
    unsigned destroys;
    unsigned ticks;
    bool fail;
    bool defer_home;
} app_state_t;

typedef struct {
    microapp_t core;
    int views[2];
    bool used[2];
    bool no_memory;
    unsigned deleted;
    void *visible;
    microapp_status_t nested_result;
    microapp_info_t info[3];
    microapp_app_t app[3];
    app_state_t state[3];
} fixture_t;

static void *create_view(void *host)
{
    fixture_t *f = (fixture_t *)host;
    if (!f->no_memory) {
        for (size_t i = 0; i < 2; ++i) {
            if (!f->used[i]) {
                f->used[i] = true;
                return &f->views[i];
            }
        }
    }
    return NULL;
}

static void delete_view(void *host, void *view)
{
    fixture_t *f = (fixture_t *)host;
    for (size_t i = 0; i < 2; ++i) {
        if (view == &f->views[i]) {
            CHECK(f->used[i]);
            f->used[i] = false;
            ++f->deleted;
            return;
        }
    }
    CHECK(false);
}

static void show_view(void *host, void *view)
{
    ((fixture_t *)host)->visible = view;
}

static microapp_status_t create(void *host, void *user, void *view, void *param)
{
    fixture_t *f = (fixture_t *)host;
    app_state_t *state = (app_state_t *)user;
    (void)param;
    CHECK(view != NULL);
    ++state->creates;
    f->nested_result = microapp_launch(&f->core, "home", NULL);
    return state->fail ? MICROAPP_ERR_CREATE : MICROAPP_OK;
}

static void resume(void *host, void *user)
{
    (void)host;
    ++((app_state_t *)user)->resumes;
}

static void pause(void *host, void *user)
{
    (void)host;
    ++((app_state_t *)user)->pauses;
}

static void destroy(void *host, void *user)
{
    (void)host;
    ++((app_state_t *)user)->destroys;
}

static void tick(void *host, void *user, uint32_t elapsed)
{
    fixture_t *f = (fixture_t *)host;
    app_state_t *state = (app_state_t *)user;
    state->ticks += elapsed;
    if (state->defer_home) {
        CHECK(microapp_request_home(&f->core) == MICROAPP_OK);
    }
}

static const microapp_port_t port = {create_view, delete_view, show_view, NULL};
static const microapp_lifecycle_t lifecycle = {create, resume, pause, destroy, tick};

static void init(fixture_t *f)
{
    const char *ids[] = {"home", "a", "b"};
    memset(f, 0, sizeof(*f));
    CHECK(microapp_init(&f->core, &port, f) == MICROAPP_OK);
    for (size_t i = 0; i < 3; ++i) {
        f->info[i].id = ids[i];
        f->app[i].info = &f->info[i];
        f->app[i].lifecycle = &lifecycle;
        f->app[i].user = &f->state[i];
        CHECK(microapp_register(&f->core, &f->app[i]) == MICROAPP_OK);
    }
}

static void test_navigation(void)
{
    fixture_t f;
    init(&f);
    CHECK(microapp_launch(&f.core, "home", NULL) == MICROAPP_OK);
    CHECK(f.nested_result == MICROAPP_ERR_BUSY);
    CHECK(microapp_launch(&f.core, "a", NULL) == MICROAPP_OK);
    CHECK(microapp_launch(&f.core, "b", NULL) == MICROAPP_OK);
    CHECK(f.core.history_count == 2);
    CHECK(microapp_request_back(&f.core) == MICROAPP_OK);
    CHECK(microapp_tick(&f.core, 10) == MICROAPP_OK);
    CHECK(f.core.active == &f.app[1]);
    CHECK(f.core.history_count == 1);
    CHECK(microapp_request_back(&f.core) == MICROAPP_OK);
    CHECK(microapp_tick(&f.core, 10) == MICROAPP_OK);
    CHECK(f.core.active == &f.app[0]);
    CHECK(f.core.history_count == 0);
    CHECK(microapp_launch(&f.core, "home", NULL) == MICROAPP_OK);
    CHECK(f.state[0].creates == 2); /* Same active app is a no-op. */
    CHECK(microapp_unregister(&f.core, "home") == MICROAPP_ERR_BUSY);
    CHECK(microapp_init(&f.core, &port, &f) == MICROAPP_ERR_BUSY);
    CHECK(microapp_deinit(&f.core) == MICROAPP_OK);
    CHECK(!f.used[0] && !f.used[1]);
    CHECK(f.state[0].destroys == 2 && f.state[1].destroys == 2 && f.state[2].destroys == 1);
}

static void test_failed_launch(void)
{
    fixture_t f;
    init(&f);
    CHECK(microapp_launch(&f.core, "home", NULL) == MICROAPP_OK);
    void *view = f.core.active_view;
    f.state[1].fail = true;
    CHECK(microapp_launch(&f.core, "a", NULL) == MICROAPP_ERR_CREATE);
    CHECK(f.core.active == &f.app[0] && f.core.active_view == view);
    CHECK(f.core.history_count == 0 && f.state[0].destroys == 0 && f.deleted == 1);
    f.no_memory = true;
    CHECK(microapp_launch(&f.core, "b", NULL) == MICROAPP_ERR_NOMEM);
    CHECK(f.core.history_count == 0 && f.state[2].creates == 0);
    CHECK(microapp_deinit(&f.core) == MICROAPP_OK);
}

static void test_deferred_and_unregister(void)
{
    fixture_t f;
    init(&f);
    CHECK(microapp_launch(&f.core, "home", NULL) == MICROAPP_OK);
    char id[] = "a";
    CHECK(microapp_request_launch(&f.core, id) == MICROAPP_OK);
    id[0] = 'b'; /* Queue owns a descriptor reference, not caller's string. */
    CHECK(microapp_tick(&f.core, 20) == MICROAPP_OK);
    CHECK(f.core.active == &f.app[1]);
    CHECK(microapp_request_launch(&f.core, "b") == MICROAPP_OK);
    CHECK(microapp_unregister(&f.core, "b") == MICROAPP_OK);
    CHECK(f.core.pending == NULL && microapp_count(&f.core) == 2);
    CHECK(microapp_at(&f.core, 2) == NULL);
    f.state[1].defer_home = true;
    CHECK(microapp_tick(&f.core, 20) == MICROAPP_OK);
    CHECK(f.core.active == &f.app[1] && f.core.pending == &f.app[0]);
    CHECK(microapp_tick(&f.core, 20) == MICROAPP_OK);
    CHECK(f.core.active == &f.app[0] && f.state[1].destroys == 1);
    CHECK(microapp_deinit(&f.core) == MICROAPP_OK);
}

static void test_registry_and_crc(void)
{
    fixture_t f;
    microapp_info_t info[MICROAPP_CAPACITY];
    microapp_app_t apps[MICROAPP_CAPACITY];
    char ids[MICROAPP_CAPACITY][8];
    memset(&f, 0, sizeof(f));
    CHECK(microapp_register(&f.core, NULL) == MICROAPP_ERR_PARAM);
    CHECK(microapp_init(&f.core, &port, &f) == MICROAPP_OK);
    memset(info, 0, sizeof(info));
    memset(apps, 0, sizeof(apps));
    for (size_t i = 0; i < MICROAPP_CAPACITY; ++i) {
        (void)snprintf(ids[i], sizeof(ids[i]), "app%u", (unsigned)i);
        info[i].id = ids[i];
        apps[i].info = &info[i];
        apps[i].lifecycle = &lifecycle;
        CHECK(microapp_register(&f.core, &apps[i]) == MICROAPP_OK);
    }
    CHECK(microapp_register(&f.core, &apps[0]) == MICROAPP_ERR_BUSY);
    microapp_info_t extra_info = {"extra", NULL, NULL, NULL, NULL};
    microapp_app_t extra = {&extra_info, &lifecycle, NULL};
    CHECK(microapp_register(&f.core, &extra) == MICROAPP_ERR_NOMEM);
    CHECK(microapp_request_launch(&f.core, "missing") == MICROAPP_ERR_NOT_FOUND);
    CHECK(microapp_deinit(&f.core) == MICROAPP_OK);
    const uint8_t *data = (const uint8_t *)"123456789";
    CHECK(microapp_crc32(data, 9) == UINT32_C(0xcbf43926));
    CHECK(microapp_crc32_update(microapp_crc32(data, 4), data + 4, 5) == UINT32_C(0xcbf43926));
    CHECK(microapp_crc32(NULL, 0) == 0);
}

int main(void)
{
    CHECK(sizeof(microapp_abi_header_t) == 12);
    CHECK(MICROAPP_ABI_REASON_DESTROY == 5);
    CHECK(MICROAPP_ABI_ERR_NOT_FOUND == -5);
    test_navigation();
    test_failed_launch();
    test_deferred_and_unregister();
    test_registry_and_crc();
    puts("PASS: navigation, failed launch, deferred callbacks, registry and CRC32");
    return 0;
}
