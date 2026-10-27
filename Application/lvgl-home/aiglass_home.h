#ifndef AIGLASS_HOME_H
#define AIGLASS_HOME_H
#include "lvgl.h"
#include "aiglass_services.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AIGLASS_HOME_WIDTH 640
#define AIGLASS_HOME_HEIGHT 480
typedef enum {
    AIGLASS_PAGE_MENU, AIGLASS_PAGE_CHAT, AIGLASS_PAGE_TRANSLATE,
    AIGLASS_PAGE_PHONE, AIGLASS_PAGE_RECORDER, AIGLASS_PAGE_CAMERA,
    AIGLASS_PAGE_FILES, AIGLASS_PAGE_NOTES, AIGLASS_PAGE_BROWSER,
    AIGLASS_PAGE_SETTINGS, AIGLASS_PAGE_APPS, AIGLASS_PAGE_GAME,
    AIGLASS_PAGE_FOCUS, AIGLASS_PAGE_NAVIGATION, AIGLASS_PAGE_RIDE, AIGLASS_PAGE_PAIRING
} aiglass_page_t;
typedef struct {
    lv_coord_t width, height; /* width >=640, height >=480; default 640 x 480. */
    const lv_font_t *font; /* NULL: bundled font; custom line_height must be 1..22. */
    ag_services_t *services; /* Borrowed if supplied; otherwise created/owned. */
    const ag_services_config_t *services_config;
    const char *input_device; /* Optional Linux evdev path. */
} aiglass_home_config_t;
/* LVGL 8.3/8.4. Call all UI APIs on the LVGL thread after display initialization. */
lv_obj_t *aiglass_home_create(lv_obj_t *parent);
lv_obj_t *aiglass_home_create_with_config(lv_obj_t *parent,
                                        const aiglass_home_config_t *config);
lv_obj_t *aiglass_home_show(void);
/* Navigation is deferred to the next LVGL timer tick. */
void aiglass_home_open(lv_obj_t *view, aiglass_page_t page);
void aiglass_home_key(lv_obj_t *view, uint32_t lv_key);
/* Attach the application's keypad/encoder input device to this group. */
lv_group_t *aiglass_home_group(lv_obj_t *view);
aiglass_page_t aiglass_home_page(lv_obj_t *view);
/* lv_obj_del(view) releases timers, evdev and owned Linux services. */
#ifdef __cplusplus
}
#endif
#endif
