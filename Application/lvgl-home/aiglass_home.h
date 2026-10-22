#ifndef AIGLASS_HOME_H
#define AIGLASS_HOME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIGLASS_HOME_WIDTH  320
#define AIGLASS_HOME_HEIGHT 320

/*
 * LVGL 8.x. Call from the LVGL thread after display initialization.
 * Creates a transparent 320 x 320 view centered inside parent.
 * Delete the returned object with lv_obj_del(); its clock timer is freed too.
 * Returns NULL if parent is NULL or the clock state cannot be allocated.
 */
lv_obj_t *aiglass_home_create(lv_obj_t *parent);

/*
 * Creates and loads a dedicated transparent screen containing the home view.
 * Returns that screen, or NULL on allocation failure.
 * The caller retains ownership of any previously active screen.
 */
lv_obj_t *aiglass_home_show(void);

#ifdef __cplusplus
}
#endif

#endif /* AIGLASS_HOME_H */
