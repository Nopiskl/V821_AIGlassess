/*
 * Minimal glasses HUD for LVGL 8.x: text, outlines and a local system clock.
 * No images, external fonts, framebuffer driver or LVGL sources are bundled.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "aiglass_home.h"

#include <time.h>

/* Change these three colors to customize the HUD. There is no background fill. */
#ifndef AIGLASS_HOME_TEXT_COLOR
#define AIGLASS_HOME_TEXT_COLOR   0xFFFFFF
#endif
#ifndef AIGLASS_HOME_MUTED_COLOR
#define AIGLASS_HOME_MUTED_COLOR  0xA0AAA7
#endif
#ifndef AIGLASS_HOME_ACCENT_COLOR
#define AIGLASS_HOME_ACCENT_COLOR 0xB8FFE0
#endif

typedef struct {
    lv_obj_t *date;
    lv_obj_t *weekday;
    lv_obj_t *clock;
    lv_obj_t *seconds;
    lv_obj_t *pulse;
    lv_timer_t *timer;
} aiglass_home_state_t;

static const lv_font_t *clock_font(void)
{
#if LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_40
    return &lv_font_montserrat_40;
#elif LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *small_font(void)
{
#if LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *title_font(void)
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

static lv_color_t text_color(void)
{
    return lv_color_hex(AIGLASS_HOME_TEXT_COLOR);
}

static lv_color_t muted_color(void)
{
    return lv_color_hex(AIGLASS_HOME_MUTED_COLOR);
}

static lv_color_t accent_color(void)
{
    return lv_color_hex(AIGLASS_HOME_ACCENT_COLOR);
}

static lv_obj_t *plain_object(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return object;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            lv_coord_t x, lv_coord_t y,
                            lv_coord_t width, lv_coord_t height,
                            const lv_font_t *font, lv_color_t color,
                            lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_outline(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height,
                              lv_coord_t radius, lv_color_t color)
{
    lv_obj_t *outline = plain_object(parent, x, y, width, height);

    lv_obj_set_style_border_width(outline, 1, 0);
    lv_obj_set_style_border_color(outline, color, 0);
    lv_obj_set_style_border_opa(outline, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outline, radius, 0);
    return outline;
}

/* Thin rectangles form strokes without retaining point arrays or image assets. */
static lv_obj_t *make_stroke(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             lv_color_t color)
{
    lv_obj_t *stroke = plain_object(parent, x, y, width, height);

    lv_obj_set_style_bg_color(stroke, color, 0);
    lv_obj_set_style_bg_opa(stroke, LV_OPA_COVER, 0);
    return stroke;
}

static void make_corner(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        int right, int bottom)
{
    const lv_coord_t length = 16;
    const lv_coord_t thickness = 2;

    make_stroke(parent, right ? x - length : x, y,
                length, thickness, accent_color());
    make_stroke(parent, right ? x - thickness : x,
                bottom ? y - length + thickness : y,
                thickness, length, accent_color());
}

static void update_clock(aiglass_home_state_t *state)
{
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    time_t now = time(NULL);
    struct tm local;

    if (now == (time_t)-1 || localtime_r(&now, &local) == NULL) {
        lv_label_set_text(state->date, "---- / -- / --");
        lv_label_set_text(state->weekday, "---");
        lv_label_set_text(state->clock, "--:--");
        lv_label_set_text(state->seconds, "SECONDS --");
        lv_obj_set_style_bg_opa(state->pulse, LV_OPA_30, 0);
        return;
    }

    lv_label_set_text_fmt(state->date, "%04d / %02d / %02d",
                          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    lv_label_set_text(state->weekday, weekdays[local.tm_wday]);
    lv_label_set_text_fmt(state->clock, "%02d:%02d", local.tm_hour, local.tm_min);
    lv_label_set_text_fmt(state->seconds, "SECONDS %02d", local.tm_sec);
    lv_obj_set_style_bg_opa(state->pulse,
                           (local.tm_sec & 1) ? LV_OPA_30 : LV_OPA_COVER, 0);
}

static void clock_timer_callback(lv_timer_t *timer)
{
    aiglass_home_state_t *state = (aiglass_home_state_t *)timer->user_data;

    update_clock(state);
}

static void view_delete_callback(lv_event_t *event)
{
    aiglass_home_state_t *state =
        (aiglass_home_state_t *)lv_event_get_user_data(event);

    if (state->timer != NULL) {
        lv_timer_del(state->timer);
    }
    lv_mem_free(state);
}

lv_obj_t *aiglass_home_create(lv_obj_t *parent)
{
    aiglass_home_state_t *state;
    lv_obj_t *root;
    lv_obj_t *label;

    if (parent == NULL) {
        return NULL;
    }

    state = lv_mem_alloc(sizeof(*state));
    if (state == NULL) {
        return NULL;
    }
    state->timer = NULL;

    root = plain_object(parent, 0, 0, AIGLASS_HOME_WIDTH, AIGLASS_HOME_HEIGHT);
    lv_obj_center(root);
    lv_obj_add_event_cb(root, view_delete_callback, LV_EVENT_DELETE, state);

    /* Small open corners keep the 320 x 320 HUD visually light. */
    make_corner(root, 10, 10, 0, 0);
    make_corner(root, 310, 10, 1, 0);
    make_corner(root, 10, 308, 0, 1);
    make_corner(root, 310, 308, 1, 1);

    label = make_label(root, "AIGLASS", 28, 30, 160, 22,
                       title_font(), text_color(), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(label, 3, 0);

    make_outline(root, 224, 27, 68, 26, 5, muted_color());
    make_label(root, "HOME", 224, 32, 68, 18,
                small_font(), accent_color(), LV_TEXT_ALIGN_CENTER);
    make_stroke(root, 28, 62, 264, 1, muted_color());

    state->date = make_label(root, "---- / -- / --", 28, 78, 202, 22,
                             small_font(), text_color(), LV_TEXT_ALIGN_LEFT);
    state->weekday = make_label(root, "---", 238, 78, 54, 22,
                                small_font(), accent_color(), LV_TEXT_ALIGN_RIGHT);

    /* An empty outlined clock card: only its border draws pixels. */
    make_outline(root, 20, 110, 280, 116, 10, accent_color());
    state->clock = make_label(root, "--:--", 28, 126, 264, 62,
                              clock_font(), text_color(), LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(state->clock, 2, 0);

    state->seconds = make_label(root, "SECONDS --", 28, 196, 264, 20,
                                small_font(), muted_color(), LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(state->seconds, 2, 0);

    make_outline(root, 99, 248, 122, 28, 6, muted_color());
    make_label(root, "LOCAL TIME", 99, 254, 122, 18,
                small_font(), text_color(), LV_TEXT_ALIGN_CENTER);

    make_stroke(root, 28, 292, 112, 1, muted_color());
    state->pulse = make_stroke(root, 157, 290, 6, 6, accent_color());
    lv_obj_set_style_radius(state->pulse, LV_RADIUS_CIRCLE, 0);
    make_stroke(root, 180, 292, 112, 1, muted_color());

    update_clock(state);
    state->timer = lv_timer_create(clock_timer_callback, 1000, state);
    if (state->timer == NULL) {
        lv_obj_del(root);
        return NULL;
    }

    return root;
}

lv_obj_t *aiglass_home_show(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    if (aiglass_home_create(screen) == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    lv_scr_load(screen);
    return screen;
}
