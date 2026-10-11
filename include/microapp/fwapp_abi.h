/**
 * @file fwapp_abi.h
 * @brief Stable ABI exposed to native FreeWatch .fwapp binaries.
 */
#ifndef FWAPP_ABI_H
#define FWAPP_ABI_H

#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FWAPP_ABI_MAJOR 1u
#define FWAPP_ABI_MINOR 2u

typedef enum {
    FWAPP_OK = 0,
    FWAPP_ERR_PARAM = -1,
    FWAPP_ERR_DENIED = -2,
    FWAPP_ERR_UNSUPPORTED = -3,
    FWAPP_ERR_IO = -4,
    FWAPP_ERR_NOT_FOUND = -5,
} fwapp_status_t;

typedef enum {
    FWAPP_REASON_CREATE = 1,
    FWAPP_REASON_RESUME = 2,
    FWAPP_REASON_PAUSE = 3,
    FWAPP_REASON_TICK = 4,
    FWAPP_REASON_DESTROY = 5,
} fwapp_reason_t;

typedef enum {
    FWAPP_PERMISSION_GUI         = (1u << 0),
    FWAPP_PERMISSION_LOG         = (1u << 1),
    FWAPP_PERMISSION_RESOURCE_RO = (1u << 2),
    FWAPP_PERMISSION_MOTOR       = (1u << 3),
} fwapp_permission_t;

typedef struct fwapp_context_s {
    uint32_t permissions;
    void *host_user;
} fwapp_context_t;

typedef struct {
    lv_obj_t *(*obj_create)(lv_obj_t *parent);
    void (*obj_del)(lv_obj_t *obj);
    void (*obj_set_size)(lv_obj_t *obj, lv_coord_t w, lv_coord_t h);
    void (*obj_align)(lv_obj_t *obj, lv_align_t align, lv_coord_t x, lv_coord_t y);
    void (*obj_center)(lv_obj_t *obj);
    void (*obj_set_style_bg_color)(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector);
    void (*obj_set_style_text_color)(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector);
    void (*obj_set_style_text_font)(lv_obj_t *obj, const lv_font_t *value, lv_style_selector_t selector);

    lv_obj_t *(*label_create)(lv_obj_t *parent);
    void (*label_set_text)(lv_obj_t *obj, const char *text);

    lv_obj_t *(*btn_create)(lv_obj_t *parent);
    void *(*obj_add_event_cb)(lv_obj_t *obj, lv_event_cb_t cb,
                              lv_event_code_t filter,
                              void *user_data);
    lv_event_code_t (*event_get_code)(lv_event_t *e);
    void *(*event_get_user_data)(lv_event_t *e);

    lv_color_t (*color_hex)(uint32_t c);
    const lv_font_t *font_montserrat_12;
    const lv_font_t *font_montserrat_14;
    const lv_font_t *font_montserrat_16;
} fwapp_lvgl_api_t;

typedef struct {
    void (*write)(fwapp_context_t *ctx, uint32_t level,
                  const char *tag, const char *msg);
} fwapp_log_api_t;

typedef struct {
    fwapp_status_t (*read)(fwapp_context_t *ctx, const char *name,
                           uint32_t offset, uint8_t *out,
                           size_t len, size_t *got);
} fwapp_resource_api_t;

typedef struct {
    fwapp_status_t (*motor_off)(fwapp_context_t *ctx);
    fwapp_status_t (*motor_pulse)(fwapp_context_t *ctx,
                                  uint16_t strength_per_mille,
                                  uint16_t on_ms,
                                  uint16_t off_ms,
                                  uint16_t repeat);
} fwapp_device_api_t;

typedef struct {
    fwapp_status_t (*return_home)(fwapp_context_t *ctx);
    fwapp_status_t (*return_back)(fwapp_context_t *ctx);
} fwapp_system_api_t;

typedef struct {
    lv_obj_t *(*create)(lv_obj_t *parent, lv_coord_t size,
                        lv_color_t dark_color, lv_color_t light_color);
    fwapp_status_t (*update)(lv_obj_t *obj, const void *data,
                             uint32_t data_len);
} fwapp_qrcode_api_t;

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    const fwapp_lvgl_api_t *lv;
    const fwapp_log_api_t *log;
    const fwapp_resource_api_t *res;
    const fwapp_device_api_t *dev;
    const fwapp_system_api_t *sys;
    const fwapp_qrcode_api_t *qrcode;
} fwapp_host_api_t;

typedef fwapp_status_t (*fwapp_main_fn)(const fwapp_host_api_t *api,
                                        fwapp_context_t *ctx,
                                        fwapp_reason_t reason,
                                        void *arg);

#ifdef __cplusplus
}
#endif

#endif /* FWAPP_ABI_H */
