#define _POSIX_C_SOURCE 200809L
#include "aiglass_home.h"
#include "aiglass_game.h"
#include "aiglass_json.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR < 3
#error "aiglass-home requires LVGL 8.3 or 8.4"
#endif
#if !LV_USE_USER_DATA || !LV_USE_LABEL || !LV_USE_BTN || !LV_USE_TEXTAREA || !LV_USE_KEYBOARD || !LV_USE_SLIDER
#error "Enable LV_USE_USER_DATA, LABEL, BTN, TEXTAREA, KEYBOARD and SLIDER"
#endif

LV_FONT_DECLARE(aiglass_font_16);
#define GREEN 0x42ff80
#define DIM_GREEN 0x208747
#define PANEL_GREEN 0x062214
#define UI_MARGIN 12
#define UI_HEADER 40
#define UI_FOOTER 88
#define UI_GAP 12
#define UI_RAIL 88
#define UI_BUTTON 36

typedef struct ui_state ui_state_t;
typedef enum {
    ACT_PAGE, ACT_BACK, ACT_EDIT, ACT_EDITOR_SAVE, ACT_EDITOR_CANCEL,
    ACT_SEND, ACT_VOICE, ACT_LIVE, ACT_LANGUAGE, ACT_CANCEL, ACT_OPERATION,
    ACT_FOLDER, ACT_FILE, ACT_FILE_READ, ACT_FILE_PLAY, ACT_FILE_DELETE,
    ACT_NOTE_SAVE, ACT_BROWSER, ACT_DOWNLOAD, ACT_GAME_MODE, ACT_GAME_KEY,
    ACT_GAME_PAUSE, ACT_FOCUS_TIMER, ACT_FOCUS_TAP, ACT_FOCUS_SAVE, ACT_UNPAIR
} action_t;
typedef struct { ui_state_t *ui; action_t action; int value; char argument[160]; } binding_t;

struct ui_state {
    lv_obj_t *root, *body, *clock, *status, *footer, *back, *title, *notification;
    lv_obj_t *output, *output_scroll, *input_hint, *dynamic;
    lv_obj_t *modal, *editor, *keyboard, *game;
    lv_group_t *group;
    lv_timer_t *timer;
    const lv_font_t *font;
    ag_services_t *services;
    bool owns_services, rebuild, close_editor, live_translation, cancelled, note_loaded, note_ready, stop_after_cancel;
    int input_fd, service_error;
    lv_coord_t width, height;
    aiglass_page_t page, history[16];
    unsigned history_count, language, game_mode, edit_field;
    uint32_t pending_id, record_started, last_second, live_next;
    ag_operation_t pending_operation;
    char draft[AG_INPUT_MAX], note[AG_INPUT_MAX], note_name[128];
    char url[AG_INPUT_MAX], download_name[128];
    char chat[AG_TEXT_MAX], translation[AG_TEXT_MAX], browser[AG_TEXT_MAX];
    char file_list[AG_TEXT_MAX], file_preview[AG_TEXT_MAX];
    char folder[16], selected_file[160], phone_message[AG_INPUT_MAX];
    bool delete_confirm, focus_running, focus_loaded;
    bool hardware_available, phone_paired, unpair_confirm;
    uint64_t message_sequence;
    uint32_t focus_started, focus_seconds, merit;
    uint32_t settings_due;
    int brightness;
    unsigned settings_dirty;
};

static const char *const titles[] = {
    "菜单", "AI 助手", "实时翻译", "手机互联", "录音机", "相机",
    "文件管理", "记事本", "文本浏览", "设置", "应用", "小游戏",
    "专注面板", "导航", "骑行", "设备配对"
};
static const char *const languages[] = {"英语", "中文", "日语"};
static const char *const language_codes[] = {"en", "zh", "ja"};
static const char *const game_names[] = {"飞机大战", "赛车", "恐龙跑酷", "靶场"};

static void render_page(ui_state_t *s);
static void open_editor(ui_state_t *s, unsigned field);
static void on_action(lv_event_t *event);

static void copy_text(char *out, size_t size, const char *text)
{
    if (!size) return;
    if (!text) text = "";
    size_t n = strnlen(text, size - 1);
    if (text[n]) while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
    memcpy(out, text, n);
    out[n] = '\0';
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(object, (lv_coord_t)width, (lv_coord_t)height);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return object;
}

static lv_obj_t *label(ui_state_t *s, lv_obj_t *parent, const char *text,
                       int x, int y, int width, int height)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(object, (lv_coord_t)width, (lv_coord_t)height);
    lv_obj_set_style_text_font(object, s->font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(GREEN), 0);
    lv_obj_set_style_text_line_space(object, 4, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_label_set_text(object, text);
    return object;
}

static void outline(lv_obj_t *object, bool selected)
{
    lv_obj_set_style_radius(object, 7, 0);
    lv_obj_set_style_border_width(object, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(selected ? GREEN : DIM_GREEN), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(PANEL_GREEN), 0);
    lv_obj_set_style_bg_opa(object, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void bind(ui_state_t *s, lv_obj_t *object, action_t action, int value, const char *argument)
{
    binding_t *binding = calloc(1, sizeof(*binding));
    if (!binding) { lv_obj_add_state(object, LV_STATE_DISABLED); return; }
    binding->ui = s;
    binding->action = action;
    binding->value = value;
    copy_text(binding->argument, sizeof(binding->argument), argument);
    lv_obj_add_event_cb(object, on_action, LV_EVENT_ALL, binding);
}

static lv_obj_t *button(ui_state_t *s, lv_obj_t *parent, const char *text,
                        int x, int y, int width, int height,
                        action_t action, int value, const char *argument)
{
    lv_obj_t *object = lv_btn_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(object, (lv_coord_t)width, (lv_coord_t)height);
    outline(object, false);
    lv_obj_set_style_border_color(object, lv_color_hex(GREEN), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(object, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(object, lv_color_hex(PANEL_GREEN), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *caption = label(s, object, text, 3, (height - s->font->line_height) / 2,
                              width - 6, s->font->line_height);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    bind(s, object, action, value, argument);
    lv_group_add_obj(s->group, object);
    if (!s->hardware_available && (action == ACT_VOICE || action == ACT_LIVE ||
        (action == ACT_OPERATION && (value == AG_PHOTO || value == AG_AUDIO_START ||
         value == AG_VIDEO_START || value == AG_MEDIA_STOP)))) {
        lv_obj_add_state(object, LV_STATE_DISABLED);
        lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
        lv_group_remove_obj(object);
    }
    return object;
}

static void message(ui_state_t *s, const char *text)
{
    lv_label_set_text(s->footer, text ? text : "");
}

static uint32_t submit(ui_state_t *s, ag_operation_t operation, const char *text, const char *argument)
{
    if (!s->services) { message(s, "Linux 服务不可用"); return 0; }
    if (s->pending_id) { message(s, "请等待当前操作或先取消"); return 0; }
    uint32_t id = ag_services_submit(s->services, operation, text, argument);
    if (!id) { message(s, strerror(errno)); return 0; }
    s->pending_id = id;
    s->pending_operation = operation;
    s->cancelled = false;
    message(s, "处理中...");
    return id;
}

static void cancel_request(ui_state_t *s)
{
    s->live_translation = false;
    if (s->pending_id) { s->cancelled = true; ag_services_cancel(s->services, s->pending_id); }
    message(s, "已取消");
}

static void navigate(ui_state_t *s, aiglass_page_t page, bool remember)
{
    if ((unsigned)page >= sizeof(titles) / sizeof(titles[0])) return;
    if (page != s->page) {
        if (remember && s->history_count < 16) s->history[s->history_count++] = s->page;
        if (s->live_translation) cancel_request(s);
        s->page = page;
        if (page == AIGLASS_PAGE_NOTES && !s->note_ready) s->note_loaded = false;
    }
    s->rebuild = true;
}

static void back(ui_state_t *s)
{
    if (s->modal) { s->rebuild = s->close_editor = true; return; }
    if (s->page == AIGLASS_PAGE_FILES && s->selected_file[0]) {
        s->selected_file[0] = s->file_preview[0] = '\0';
        s->delete_confirm = false;
        s->rebuild = true;
        return;
    }
    aiglass_page_t page = s->history_count ? s->history[--s->history_count] : AIGLASS_PAGE_MENU;
    navigate(s, page, false);
}

static void draw_line(lv_draw_ctx_t *context, int x1, int y1, int x2, int y2)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(GREEN);
    dsc.width = 2;
    dsc.opa = LV_OPA_COVER;
    lv_point_t a = {(lv_coord_t)x1, (lv_coord_t)y1}, b = {(lv_coord_t)x2, (lv_coord_t)y2};
    lv_draw_line(context, &dsc, &a, &b);
}

static void draw_rect(lv_draw_ctx_t *context, int x, int y, int width, int height, int radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(GREEN);
    dsc.border_width = 2;
    dsc.border_opa = LV_OPA_COVER;
    dsc.radius = (lv_coord_t)radius;
    lv_area_t area = {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + width - 1), (lv_coord_t)(y + height - 1)};
    lv_draw_rect(context, &dsc, &area);
}

static void draw_icon(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    int x = area.x1, y = area.y1;
    unsigned kind = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    switch (kind) {
    case AIGLASS_PAGE_PHONE:
        draw_rect(context, x + 8, y + 2, 18, 30, 3);
        draw_line(context, x + 14, y + 27, x + 20, y + 27); break;
    case AIGLASS_PAGE_RECORDER:
        draw_rect(context, x + 12, y + 2, 10, 21, 5);
        draw_line(context, x + 7, y + 15, x + 7, y + 23);
        draw_line(context, x + 27, y + 15, x + 27, y + 23);
        draw_line(context, x + 7, y + 23, x + 27, y + 23);
        draw_line(context, x + 17, y + 23, x + 17, y + 32); break;
    case AIGLASS_PAGE_CAMERA:
        draw_rect(context, x + 3, y + 9, 29, 22, 4);
        draw_rect(context, x + 12, y + 3, 12, 7, 2);
        draw_rect(context, x + 12, y + 14, 12, 12, 6); break;
    case AIGLASS_PAGE_TRANSLATE:
        draw_line(context, x + 4, y + 25, x + 12, y + 4);
        draw_line(context, x + 12, y + 4, x + 20, y + 25);
        draw_line(context, x + 7, y + 18, x + 17, y + 18);
        draw_line(context, x + 20, y + 11, x + 32, y + 11);
        draw_line(context, x + 26, y + 6, x + 26, y + 27);
        draw_line(context, x + 21, y + 29, x + 32, y + 20); break;
    case AIGLASS_PAGE_NAVIGATION:
        draw_rect(context, x + 7, y + 2, 22, 24, 11);
        draw_rect(context, x + 14, y + 8, 8, 8, 4);
        draw_line(context, x + 9, y + 24, x + 18, y + 33);
        draw_line(context, x + 18, y + 33, x + 27, y + 24); break;
    case AIGLASS_PAGE_RIDE:
        draw_rect(context, x + 1, y + 17, 14, 14, 7);
        draw_rect(context, x + 22, y + 17, 14, 14, 7);
        draw_line(context, x + 8, y + 24, x + 18, y + 9);
        draw_line(context, x + 18, y + 9, x + 29, y + 24);
        draw_line(context, x + 8, y + 24, x + 22, y + 24);
        draw_line(context, x + 14, y + 6, x + 23, y + 6); break;
    case AIGLASS_PAGE_SETTINGS:
        draw_rect(context, x + 7, y + 7, 22, 22, 11);
        draw_rect(context, x + 14, y + 14, 8, 8, 4);
        draw_line(context, x + 18, y + 1, x + 18, y + 7);
        draw_line(context, x + 18, y + 29, x + 18, y + 35);
        draw_line(context, x + 1, y + 18, x + 7, y + 18);
        draw_line(context, x + 29, y + 18, x + 35, y + 18);
        draw_line(context, x + 5, y + 5, x + 10, y + 10);
        draw_line(context, x + 26, y + 26, x + 31, y + 31);
        draw_line(context, x + 5, y + 31, x + 10, y + 26);
        draw_line(context, x + 26, y + 10, x + 31, y + 5); break;
    case AIGLASS_PAGE_FILES:
        draw_rect(context, x + 2, y + 10, 32, 22, 3);
        draw_line(context, x + 3, y + 10, x + 3, y + 4);
        draw_line(context, x + 3, y + 4, x + 15, y + 4);
        draw_line(context, x + 15, y + 4, x + 21, y + 10); break;
    case AIGLASS_PAGE_NOTES:
        draw_rect(context, x + 6, y + 2, 24, 32, 2);
        for (int row = 0; row < 3; ++row)
            draw_line(context, x + 11, y + 11 + row * 7, x + 25, y + 11 + row * 7);
        break;
    case AIGLASS_PAGE_BROWSER:
        draw_rect(context, x + 2, y + 2, 32, 32, 16);
        draw_rect(context, x + 12, y + 2, 12, 32, 6);
        draw_line(context, x + 2, y + 18, x + 34, y + 18); break;
    case AIGLASS_PAGE_GAME:
        draw_rect(context, x + 2, y + 9, 32, 22, 7);
        draw_line(context, x + 6, y + 20, x + 16, y + 20);
        draw_line(context, x + 11, y + 15, x + 11, y + 25);
        draw_rect(context, x + 22, y + 15, 4, 4, 2);
        draw_rect(context, x + 27, y + 22, 4, 4, 2); break;
    case AIGLASS_PAGE_FOCUS:
        draw_rect(context, x + 12, y + 2, 12, 12, 6);
        draw_line(context, x + 18, y + 14, x + 18, y + 26);
        draw_line(context, x + 18, y + 17, x + 5, y + 25);
        draw_line(context, x + 18, y + 17, x + 31, y + 25);
        draw_line(context, x + 5, y + 31, x + 31, y + 31);
        draw_line(context, x + 5, y + 31, x + 18, y + 25);
        draw_line(context, x + 31, y + 31, x + 18, y + 25); break;
    default:
        draw_rect(context, x + 8, y + 8, 20, 20, kind == AIGLASS_PAGE_CHAT ? 10 : 3);
        for (int i = 0; i < 3; ++i) {
            draw_line(context, x + 11 + i * 7, y + 2, x + 11 + i * 7, y + 7);
            draw_line(context, x + 11 + i * 7, y + 28, x + 11 + i * 7, y + 33);
        }
        draw_line(context, x + 2, y + 17, x + 8, y + 17);
        draw_line(context, x + 28, y + 17, x + 34, y + 17); break;
    }
}

static lv_obj_t *icon_button(ui_state_t *s, lv_obj_t *parent, const char *text,
                             int x, int y, int width, int height, aiglass_page_t page)
{
    lv_obj_t *object = button(s, parent, "", x, y, width, height, ACT_PAGE, page, NULL);
    lv_obj_t *icon = box(object, (width - 36) / 2, (height - 68) / 2, 36, 36);
    lv_obj_add_event_cb(icon, draw_icon, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)page);
    lv_obj_t *caption = label(s, object, text, 4, height - 32, width - 8, 24);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    return object;
}

static void transcript(ui_state_t *s, lv_obj_t *parent, int x, int y, int width, int height, const char *text)
{
    s->output_scroll = box(parent, x, y, width, height);
    outline(s->output_scroll, false);
    lv_obj_add_flag(s->output_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s->output_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s->output_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(s->output_scroll, 8, 0);
    s->output = label(s, s->output_scroll, text, 0, 0, width - 18, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s->output, LV_LABEL_LONG_WRAP);
}

static void append_text(char *buffer, size_t size, const char *prefix, const char *text)
{
    size_t existing = strlen(buffer), needed = strlen(prefix) + strlen(text) + 2;
    if (existing + needed >= size) { buffer[0] = '\0'; existing = 0; }
    if (existing) { buffer[existing++] = '\n'; buffer[existing++] = '\n'; buffer[existing] = '\0'; }
    copy_text(buffer + existing, size - existing, prefix);
    existing = strlen(buffer);
    copy_text(buffer + existing, size - existing, text);
}

static void make_menu(ui_state_t *s, int width, int height)
{
    static const aiglass_page_t pages[] = {
        AIGLASS_PAGE_NAVIGATION, AIGLASS_PAGE_RIDE, AIGLASS_PAGE_PHONE, AIGLASS_PAGE_RECORDER,
        AIGLASS_PAGE_TRANSLATE, AIGLASS_PAGE_CAMERA, AIGLASS_PAGE_CHAT, AIGLASS_PAGE_SETTINGS
    };
    int cell_w = (width - 3 * UI_GAP) / 4;
    int cell_h = (height - UI_BUTTON - 2 * UI_GAP) / 2;
    for (unsigned i = 0; i < 8; ++i)
        icon_button(s, s->body, titles[pages[i]], (int)(i % 4) * (cell_w + UI_GAP),
                    (int)(i / 4) * (cell_h + UI_GAP), cell_w, cell_h, pages[i]);
    button(s, s->body, "全部应用", 0, height - UI_BUTTON, width, UI_BUTTON, ACT_PAGE, AIGLASS_PAGE_APPS, NULL);
}

static lv_obj_t *make_rail(ui_state_t *s, int width, int height)
{
    static const aiglass_page_t pages[] = {AIGLASS_PAGE_TRANSLATE, AIGLASS_PAGE_CHAT, AIGLASS_PAGE_PHONE};
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *item = icon_button(s, s->body, titles[pages[i]], 0, (int)i * ((height - 2 * UI_GAP) / 3 + UI_GAP), UI_RAIL, (height - 2 * UI_GAP) / 3, pages[i]);
        if (pages[i] == s->page) outline(item, true);
    }
    return box(s->body, UI_RAIL + UI_GAP, 0, width - UI_RAIL - UI_GAP, height);
}

static void make_conversation(ui_state_t *s, int width, int height)
{
    bool translation = s->page == AIGLASS_PAGE_TRANSLATE;
    lv_obj_t *content = make_rail(s, width, height);
    int w = width - UI_RAIL - UI_GAP;
    char heading[64];
    snprintf(heading, sizeof(heading), "译为%s", languages[s->language]);
    if (translation) button(s, content, heading, 0, 0, w, 28, ACT_LANGUAGE, 0, NULL);
    else label(s, content, "AI 助手", 0, 2, w, 24);
    s->input_hint = label(s, content, s->draft[0] ? s->draft : "输入文字或使用语音", 0, 34, w, 24);
    transcript(s, content, 0, 62, w, height - 138,
               translation ? (s->translation[0] ? s->translation : "翻译结果将显示在这里") :
                             (s->chat[0] ? s->chat : "输入问题，开始对话"));
    int bw = (w - 12) / 3;
    button(s, content, "输入", 0, height - 68, bw, 30, ACT_EDIT, 0, NULL);
    button(s, content, "发送", bw + 6, height - 68, bw, 30, ACT_SEND, 0, NULL);
    button(s, content, "语音", 2 * (bw + 6), height - 68, bw, 30, ACT_VOICE, 0, NULL);
    if (translation) {
        button(s, content, s->live_translation ? "停止字幕" : "连续字幕", 0, height - 32, (w - 6) / 2, 30, ACT_LIVE, 0, NULL);
        button(s, content, "取消", (w + 6) / 2, height - 32, (w - 6) / 2, 30, ACT_CANCEL, 0, NULL);
    } else button(s, content, "取消请求", 0, height - 32, w, 30, ACT_CANCEL, 0, NULL);
}

static void make_pairing(ui_state_t *s, int width, int height)
{
    ag_status_t status;
    ag_services_status(s->services, &status);
    int left_width = (width - UI_GAP) / 2;
    lv_obj_t *card = box(s->body, 0, 0, left_width, height - 48);
    outline(card, true);
    label(s, card, status.phone_paired ? "手机已配对" : "首次连接", 16, 16, left_width - 32, 24);
    char identity[64];
    snprintf(identity, sizeof(identity), "V821 · %.8s", status.device_id);
    label(s, card, identity, 16, 56, left_width - 32, 24);
    lv_obj_t *code = label(s, card, status.phone_paired ? "绑定已保存" : status.pairing_code,
                          16, 104, left_width - 32, 42);
    lv_obj_set_style_text_letter_space(code, status.phone_paired ? 0 : 10, 0);
    label(s, card, status.phone_paired ? "重启后可恢复连接" : "在手机端输入六位配对码", 16, 160, left_width - 32, 48);
    lv_obj_t *instructions = label(s, s->body,
        status.phone_paired ? "连接状态与文件由同一服务管理。\n\n更换手机时，先解除当前绑定。" :
        "1. 打开手机伴侣，查找设备\n\n2. 核对设备编号，输入配对码\n\n3. 配对成功后进入应用",
        left_width + UI_GAP, 12, width - left_width - UI_GAP, height - 72);
    lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
    button(s, s->body, status.phone_paired ? (s->unpair_confirm ? "确认解除" : "解除配对") : "刷新配对码",
           0, height - 36, left_width, 36, ACT_UNPAIR, 0, NULL);
    button(s, s->body, "进入应用", left_width + UI_GAP, height - 36,
           width - left_width - UI_GAP, 36, ACT_PAGE, AIGLASS_PAGE_MENU, NULL);
}

static void make_phone(ui_state_t *s, int width, int height)
{
    lv_obj_t *content = make_rail(s, width, height);
    int w = width - UI_RAIL - UI_GAP;
    s->dynamic = label(s, content, "等待手机连接", 0, 0, w, 82);
    lv_label_set_long_mode(s->dynamic, LV_LABEL_LONG_WRAP);
    transcript(s, content, 0, 88, w, height - 132, s->phone_message[0] ? s->phone_message : "手机消息与文件接收状态");
    button(s, content, "文件", 0, height - 32, (w - 6) / 2, 30, ACT_PAGE, AIGLASS_PAGE_FILES, NULL);
    button(s, content, "设备配对", (w + 6) / 2, height - 32, (w - 6) / 2, 30, ACT_PAGE, AIGLASS_PAGE_PAIRING, NULL);
}

static void make_media(ui_state_t *s, int width, int height)
{
    bool camera = s->page == AIGLASS_PAGE_CAMERA;
    s->dynamic = label(s, s->body, "准备就绪", 8, 12, width - 16, 64);
    lv_label_set_long_mode(s->dynamic, LV_LABEL_LONG_WRAP);
    label(s, s->body, camera ? "照片与录像保存至媒体目录" : "本地麦克风录音，保存为 WAV", 8, 92, width - 16, 48);
    int bw = (width - 16) / 3;
    button(s, s->body, camera ? "拍照" : "录音", 0, height - 78, bw, 34,
           ACT_OPERATION, camera ? AG_PHOTO : AG_AUDIO_START, NULL);
    button(s, s->body, camera ? "录像" : "文件", bw + 8, height - 78, bw, 34,
           camera ? ACT_OPERATION : ACT_PAGE, camera ? AG_VIDEO_START : AIGLASS_PAGE_FILES, NULL);
    button(s, s->body, "停止", 2 * (bw + 8), height - 78, bw, 34, ACT_OPERATION, AG_MEDIA_STOP, NULL);
    button(s, s->body, "媒体文件", 0, height - 34, width, 34, ACT_FOLDER, 0, "media");
}

static void make_files(ui_state_t *s, int width, int height)
{
    const char *names[] = {"收件箱", "媒体", "笔记"};
    const char *folders[] = {"inbox", "media", "notes"};
    int bw = (width - 12) / 3;
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *b = button(s, s->body, names[i], (int)i * (bw + 6), 0, bw, 30, ACT_FOLDER, 0, folders[i]);
        if (!strcmp(s->folder, folders[i])) outline(b, true);
    }
    if (s->selected_file[0]) {
        int action_width = (width - 18) / 4;
        label(s, s->body, s->selected_file, 0, 38, width, 24);
        transcript(s, s->body, 0, 66, width, height - 110,
                   s->file_preview[0] ? s->file_preview : "文本可查看；媒体可传至手机");
        button(s, s->body, "查看", 0, height - 32, action_width, 30, ACT_FILE_READ, 0, NULL);
        lv_obj_t *play = button(s, s->body, "播放", action_width + 6, height - 32, action_width, 30, ACT_FILE_PLAY, 0, NULL);
        lv_obj_add_state(play, LV_STATE_DISABLED);
        lv_obj_set_style_opa(play, LV_OPA_40, LV_STATE_DISABLED);
        lv_group_remove_obj(play);
        button(s, s->body, "停止", 2 * (action_width + 6), height - 32, action_width, 30, ACT_OPERATION, AG_MEDIA_STOP, NULL);
        button(s, s->body, s->delete_confirm ? "确认删除" : "删除", 3 * (action_width + 6), height - 32, action_width, 30, ACT_FILE_DELETE, 0, NULL);
    } else {
        lv_obj_t *list = box(s->body, 0, 38, width, height - 80);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        cJSON *items = ag_json_parse(s->file_list);
        int row = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
            if (cJSON_IsString(name)) {
                button(s, list, name->valuestring, 0, row * 40, width - 6, 34, ACT_FILE, 0, name->valuestring);
                ++row;
            }
        }
        cJSON_Delete(items);
        if (!row) label(s, list, "目录为空，或点击刷新", 8, 12, width - 16, 24);
        button(s, s->body, "刷新文件", 0, height - 32, width, 30, ACT_OPERATION, AG_LIST_FILES, s->folder);
    }
}

static void make_notes(ui_state_t *s, int width, int height)
{
    button(s, s->body, s->note_name, 0, 0, width, 30, ACT_EDIT, 4, NULL);
    transcript(s, s->body, 0, 38, width, height - 80, s->note[0] ? s->note : "记录文字内容");
    button(s, s->body, "编辑", 0, height - 32, (width - 8) / 2, 30, ACT_EDIT, 1, NULL);
    button(s, s->body, "保存", (width + 8) / 2, height - 32, (width - 8) / 2, 30, ACT_NOTE_SAVE, 0, NULL);
}

static void make_browser(ui_state_t *s, int width, int height)
{
    button(s, s->body, s->url[0] ? s->url : "输入网页地址", 0, 0, width, 30, ACT_EDIT, 2, NULL);
    transcript(s, s->body, 0, 38, width, height - 80, s->browser[0] ? s->browser : "显示网页文字内容");
    button(s, s->body, "打开", 0, height - 32, (width - 8) / 2, 30, ACT_BROWSER, 0, NULL);
    button(s, s->body, "取消", (width + 8) / 2, height - 32, (width - 8) / 2, 30, ACT_CANCEL, 0, NULL);
}

static void slider_changed(lv_event_t *event)
{
    ui_state_t *s = lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    int value = lv_slider_get_value(slider);
    s->brightness = value;
    s->settings_dirty = 1;
    s->settings_due = lv_tick_get() + 300;
}

static void make_settings(ui_state_t *s, int width)
{
    label(s, s->body, "显示亮度", 4, 12, width - 8, 24);
    lv_obj_t *slider = lv_slider_create(s->body);
    lv_obj_set_pos(slider, 14, 52);
    lv_obj_set_size(slider, (lv_coord_t)(width - 28), 10);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, s->brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(DIM_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(GREEN), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_changed, LV_EVENT_VALUE_CHANGED, s);
    lv_group_add_obj(s->group, slider);
    if (!s->hardware_available) {
        lv_obj_add_state(slider, LV_STATE_DISABLED);
        lv_group_remove_obj(slider);
    }
    label(s, s->body, "音频播放 / 音量", 4, 96, width - 8, 24);
    label(s, s->body, "播放接口尚未接入", 4, 128, width - 8, 24);
    s->dynamic = label(s, s->body, "调整后应用到设备", 4, 176, width - 8, 58);
    lv_label_set_long_mode(s->dynamic, LV_LABEL_LONG_WRAP);
}

static void make_apps(ui_state_t *s, int width, int height)
{
    static const aiglass_page_t pages[] = {AIGLASS_PAGE_FILES, AIGLASS_PAGE_NOTES, AIGLASS_PAGE_BROWSER,
        AIGLASS_PAGE_GAME, AIGLASS_PAGE_FOCUS, AIGLASS_PAGE_RECORDER};
    int bw = (width - 2 * UI_GAP) / 3;
    int bh = (height - 84 - UI_GAP) / 2;
    for (unsigned i = 0; i < 6; ++i)
        icon_button(s, s->body, titles[pages[i]], (int)(i % 3) * (bw + UI_GAP),
                    (int)(i / 3) * (bh + UI_GAP), bw, bh, pages[i]);
    button(s, s->body, s->url[0] ? s->url : "输入下载地址", 0, height - 72, width, 30, ACT_EDIT, 2, NULL);
    button(s, s->body, s->download_name, 0, height - 34, width - 86, 32, ACT_EDIT, 3, NULL);
    button(s, s->body, "下载", width - 78, height - 34, 78, 32, ACT_DOWNLOAD, 0, NULL);
}

static void make_game(ui_state_t *s, int width, int height)
{
    button(s, s->body, game_names[s->game_mode], 0, 0, width - 78, 30, ACT_GAME_MODE, 0, NULL);
    button(s, s->body, "暂停", width - 70, 0, 70, 30, ACT_GAME_PAUSE, 0, NULL);
    lv_obj_t *arena = box(s->body, 0, 38, width, height - 80);
    outline(arena, false);
    s->game = aiglass_game_create(arena, s->game_mode, s->font);
    int bw = (width - 16) / 3;
    button(s, s->body, "左", 0, height - 34, bw, 32, ACT_GAME_KEY, LV_KEY_LEFT, NULL);
    button(s, s->body, "跳跃/发射", bw + 8, height - 34, bw, 32, ACT_GAME_KEY, LV_KEY_ENTER, NULL);
    button(s, s->body, "右", 2 * (bw + 8), height - 34, bw, 32, ACT_GAME_KEY, LV_KEY_RIGHT, NULL);
}

static uint32_t focus_time(ui_state_t *s)
{
    return s->focus_seconds + (s->focus_running ? (lv_tick_get() - s->focus_started) / 1000 : 0);
}

static void save_focus(ui_state_t *s)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"seconds\":%u,\"merit\":%u}", focus_time(s), s->merit);
    submit(s, AG_WRITE_NOTE, json, "focus.json");
}

static void make_focus(ui_state_t *s, int width, int height)
{
    s->dynamic = label(s, s->body, "专注计时 / 木鱼计数", 12, 12, width - 24, 134);
    lv_label_set_long_mode(s->dynamic, LV_LABEL_LONG_WRAP);
    button(s, s->body, s->focus_running ? "结束打坐" : "开始打坐", 0, height - 76, (width - 8) / 2, 34, ACT_FOCUS_TIMER, 0, NULL);
    button(s, s->body, "敲木鱼", (width + 8) / 2, height - 76, (width - 8) / 2, 34, ACT_FOCUS_TAP, 0, NULL);
    button(s, s->body, "保存进度", 0, height - 34, width, 32, ACT_FOCUS_SAVE, 0, NULL);
}

static void make_navigation(ui_state_t *s, int width, int height)
{
    s->dynamic = label(s, s->body, "等待手机导航数据", 12, 12, width - 24, height - 24);
    lv_label_set_long_mode(s->dynamic, LV_LABEL_LONG_WRAP);
}

static char *editing_text(ui_state_t *s, size_t *size)
{
    switch (s->edit_field) {
    case 1: *size = sizeof(s->note); return s->note;
    case 2: *size = sizeof(s->url); return s->url;
    case 3: *size = sizeof(s->download_name); return s->download_name;
    case 4: *size = sizeof(s->note_name); return s->note_name;
    default: *size = sizeof(s->draft); return s->draft;
    }
}

static void keyboard_event(lv_event_t *event)
{
    ui_state_t *s = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        size_t size;
        char *destination = editing_text(s, &size);
        copy_text(destination, size, lv_textarea_get_text(s->editor));
        s->rebuild = s->close_editor = true;
    } else if (lv_event_get_code(event) == LV_EVENT_CANCEL) s->rebuild = s->close_editor = true;
}

static void open_editor(ui_state_t *s, unsigned field)
{
    size_t size;
    if (s->modal) return;
    s->edit_field = field;
    const char *text = editing_text(s, &size);
    (void)size;
    lv_group_remove_all_objs(s->group);
    s->modal = box(s->root, 0, 0, s->width, s->height);
    lv_obj_set_style_bg_color(s->modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s->modal, LV_OPA_COVER, 0);
    label(s, s->modal, field == 2 ? "网页 / 下载地址" : field >= 3 ? "文件名" : "输入文字", 12, 8, s->width - 24, 24);
    s->editor = lv_textarea_create(s->modal);
    lv_obj_set_pos(s->editor, 8, 36);
    lv_obj_set_size(s->editor, s->width - 16, 64);
    lv_obj_set_style_text_font(s->editor, s->font, 0);
    lv_obj_set_style_text_color(s->editor, lv_color_hex(GREEN), 0);
    lv_obj_set_style_bg_color(s->editor, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s->editor, 6, 0);
    outline(s->editor, true);
    lv_textarea_set_max_length(s->editor, field >= 3 ? 30 : 500);
    lv_textarea_set_text(s->editor, text);
    lv_textarea_set_one_line(s->editor, field >= 2);
    lv_group_add_obj(s->group, s->editor);
    s->keyboard = lv_keyboard_create(s->modal);
    lv_obj_set_pos(s->keyboard, 0, 108);
    lv_obj_set_size(s->keyboard, s->width, s->height - 150);
    lv_obj_set_style_text_font(s->keyboard, s->font, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s->keyboard, lv_color_hex(GREEN), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s->keyboard, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s->keyboard, lv_color_hex(PANEL_GREEN), LV_PART_ITEMS);
    lv_keyboard_set_textarea(s->keyboard, s->editor);
    lv_obj_add_event_cb(s->keyboard, keyboard_event, LV_EVENT_ALL, s);
    lv_group_add_obj(s->group, s->keyboard);
    button(s, s->modal, "取消", 8, s->height - 34, (s->width - 24) / 2, 30, ACT_EDITOR_CANCEL, 0, NULL);
    button(s, s->modal, "完成", (s->width + 8) / 2, s->height - 34, (s->width - 24) / 2, 30, ACT_EDITOR_SAVE, 0, NULL);
    lv_group_focus_obj(s->keyboard);
}

static void on_action(lv_event_t *event)
{
    binding_t *binding = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        /* LVGL can emit DEFOCUSED during destruction after DELETE. Detach the
         * callback before freeing its user data, including modal buttons. */
        lv_obj_remove_event_cb(lv_event_get_current_target(event), on_action);
        free(binding);
        return;
    }
    if (code != LV_EVENT_KEY && code != LV_EVENT_CLICKED) return;
    ui_state_t *s = binding->ui;
    if (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_ESC) { back(s); return; }
    if (code != LV_EVENT_CLICKED) return;
    if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_DISABLED)) return;
    switch (binding->action) {
    case ACT_UNPAIR:
        if (s->phone_paired && !s->unpair_confirm) {
            s->unpair_confirm = true;
            s->rebuild = true;
            break;
        }
        {
            int rc = ag_services_unpair(s->services);
            s->unpair_confirm = false;
            s->rebuild = true;
            message(s, rc ? strerror(-rc) : "配对码已更新");
        }
        break;
    case ACT_PAGE: navigate(s, (aiglass_page_t)binding->value, true); break;
    case ACT_BACK: back(s); break;
    case ACT_EDIT:
        if ((binding->value == 1 || binding->value == 4) && s->services &&
            !s->note_ready) {
            message(s, "请等待笔记读取完成"); break;
        }
        open_editor(s, (unsigned)binding->value);
        break;
    case ACT_EDITOR_SAVE: {
        size_t size;
        char *destination = editing_text(s, &size);
        copy_text(destination, size, lv_textarea_get_text(s->editor));
        s->rebuild = s->close_editor = true;
        break;
    }
    case ACT_EDITOR_CANCEL: s->rebuild = s->close_editor = true; break;
    case ACT_SEND:
        if (!s->draft[0]) { message(s, "请先输入内容"); break; }
        if (submit(s, s->page == AIGLASS_PAGE_TRANSLATE ? AG_TRANSLATE : AG_CHAT,
                   s->draft, language_codes[s->language])) {
            if (s->page == AIGLASS_PAGE_CHAT) append_text(s->chat, sizeof(s->chat), "我：", s->draft);
            s->rebuild = true;
        }
        break;
    case ACT_VOICE:
        submit(s, s->page == AIGLASS_PAGE_TRANSLATE ? AG_VOICE_TRANSLATE : AG_VOICE_CHAT, "", language_codes[s->language]);
        break;
    case ACT_LIVE:
        if (s->live_translation) cancel_request(s);
        else if (submit(s, AG_VOICE_TRANSLATE, "", language_codes[s->language])) s->live_translation = true;
        s->rebuild = true;
        break;
    case ACT_LANGUAGE:
        cancel_request(s);
        s->language = (s->language + 1) % 3;
        s->rebuild = true;
        break;
    case ACT_CANCEL: cancel_request(s); break;
    case ACT_OPERATION:
        if (binding->value == AG_MEDIA_STOP && s->pending_id) {
            s->stop_after_cancel = true;
            cancel_request(s);
            break;
        }
        submit(s, (ag_operation_t)binding->value, "", binding->argument);
        break;
    case ACT_FOLDER:
        copy_text(s->folder, sizeof(s->folder), binding->argument);
        s->selected_file[0] = s->file_list[0] = s->file_preview[0] = '\0';
        s->delete_confirm = false;
        navigate(s, AIGLASS_PAGE_FILES, true);
        submit(s, AG_LIST_FILES, "", s->folder);
        break;
    case ACT_FILE:
        snprintf(s->selected_file, sizeof(s->selected_file), "%.15s/%.120s", s->folder, binding->argument);
        s->file_preview[0] = '\0';
        s->delete_confirm = false;
        s->rebuild = true;
        break;
    case ACT_FILE_READ: submit(s, AG_READ_FILE, "", s->selected_file); break;
    case ACT_FILE_PLAY: submit(s, AG_PLAY_AUDIO, "", s->selected_file); break;
    case ACT_FILE_DELETE:
        if (s->delete_confirm) submit(s, AG_DELETE_FILE, "", s->selected_file);
        else { s->delete_confirm = true; s->rebuild = true; }
        break;
    case ACT_NOTE_SAVE:
        if (!s->note_ready) { message(s, "请等待笔记读取完成"); break; }
        submit(s, AG_WRITE_NOTE, s->note, s->note_name);
        break;
    case ACT_BROWSER: submit(s, AG_BROWSE, s->url, ""); break;
    case ACT_DOWNLOAD: submit(s, AG_DOWNLOAD, s->url, s->download_name); break;
    case ACT_GAME_MODE: s->game_mode = (s->game_mode + 1) % 4; s->rebuild = true; break;
    case ACT_GAME_KEY: aiglass_game_key(s->game, (uint32_t)binding->value); break;
    case ACT_GAME_PAUSE: aiglass_game_pause(s->game); break;
    case ACT_FOCUS_TIMER:
        if (s->pending_id || (!s->focus_loaded && s->services)) { message(s, "请等待进度读写完成"); break; }
        if (s->focus_running) { s->focus_seconds = focus_time(s); s->focus_running = false; save_focus(s); }
        else { s->focus_started = lv_tick_get(); s->focus_running = true; }
        s->rebuild = true;
        break;
    case ACT_FOCUS_TAP:
        if (s->pending_id || (!s->focus_loaded && s->services)) { message(s, "请等待进度读写完成"); break; }
        if (s->merit < UINT32_MAX) ++s->merit;
        save_focus(s);
        break;
    case ACT_FOCUS_SAVE:
        if (!s->focus_loaded && s->services) { message(s, "请等待进度读取完成"); break; }
        save_focus(s);
        break;
    }
}

static void render_page(ui_state_t *s)
{
    if (s->modal) {
        lv_obj_del(s->modal);
        s->modal = s->editor = s->keyboard = NULL;
    }
    s->close_editor = false;
    lv_group_remove_all_objs(s->group);
    lv_obj_clean(s->body);
    s->output = s->output_scroll = s->input_hint = s->dynamic = s->game = NULL;
    s->rebuild = false;
    int width = s->width - 2 * UI_MARGIN;
    int height = s->height - UI_HEADER - UI_FOOTER - 2 * UI_MARGIN;
    lv_label_set_text(s->title, titles[s->page]);
    switch (s->page) {
    case AIGLASS_PAGE_PAIRING: make_pairing(s, width, height); break;
    case AIGLASS_PAGE_MENU: make_menu(s, width, height); break;
    case AIGLASS_PAGE_CHAT: case AIGLASS_PAGE_TRANSLATE: make_conversation(s, width, height); break;
    case AIGLASS_PAGE_PHONE: make_phone(s, width, height); break;
    case AIGLASS_PAGE_RECORDER: case AIGLASS_PAGE_CAMERA: make_media(s, width, height); break;
    case AIGLASS_PAGE_FILES: make_files(s, width, height); break;
    case AIGLASS_PAGE_NOTES: make_notes(s, width, height); break;
    case AIGLASS_PAGE_BROWSER: make_browser(s, width, height); break;
    case AIGLASS_PAGE_SETTINGS: make_settings(s, width); break;
    case AIGLASS_PAGE_APPS: make_apps(s, width, height); break;
    case AIGLASS_PAGE_GAME: make_game(s, width, height); break;
    case AIGLASS_PAGE_FOCUS: make_focus(s, width, height); break;
    case AIGLASS_PAGE_NAVIGATION: case AIGLASS_PAGE_RIDE: make_navigation(s, width, height); break;
    }
    if (s->page == AIGLASS_PAGE_MENU) lv_obj_add_flag(s->back, LV_OBJ_FLAG_HIDDEN);
    else { lv_obj_clear_flag(s->back, LV_OBJ_FLAG_HIDDEN); lv_group_add_obj(s->group, s->back); }
    if (s->output_scroll) {
        lv_group_add_obj(s->group, s->output_scroll);
        lv_obj_scroll_to_y(s->output_scroll, 0, LV_ANIM_OFF);
    }
    s->last_second = UINT32_MAX;
}

static void receive_result(ui_state_t *s, const ag_result_t *result)
{
    bool own = result->id && result->id == s->pending_id;
    if (result->id && !own) return;
    if (own) {
        s->pending_id = 0;
        if (s->cancelled) {
            if (result->operation == AG_READ_FILE && !s->note_ready) s->note_loaded = false;
            s->cancelled = false;
            message(s, "已取消");
            return;
        }
    }
    if (result->code) {
        if (result->code == -ENOENT && result->operation == AG_READ_FILE &&
            (!strcmp(result->detail, "notes/focus.json") || !strcmp(result->detail, "notes/note.txt"))) {
            if (!strcmp(result->detail, "notes/note.txt")) s->note_ready = true;
            message(s, "准备就绪"); return;
        }
        if (result->operation == AG_VOICE_TRANSLATE) { s->live_translation = false; s->rebuild = true; }
        message(s, result->text);
        return;
    }
    switch (result->operation) {
    case AG_CHAT: case AG_VOICE_CHAT:
        if (result->operation == AG_VOICE_CHAT) append_text(s->chat, sizeof(s->chat), "我：", result->detail);
        append_text(s->chat, sizeof(s->chat), "助手：", result->text);
        if (s->page == AIGLASS_PAGE_CHAT) s->rebuild = true;
        break;
    case AG_TRANSLATE: case AG_VOICE_TRANSLATE:
        if (result->operation == AG_VOICE_TRANSLATE) append_text(s->translation, sizeof(s->translation), "原文：", result->detail);
        append_text(s->translation, sizeof(s->translation), "译文：", result->text);
        if (s->page == AIGLASS_PAGE_TRANSLATE) s->rebuild = true;
        s->live_next = lv_tick_get() + 250;
        break;
    case AG_LIST_FILES:
        if (!strcmp(result->detail, s->folder)) {
            copy_text(s->file_list, sizeof(s->file_list), result->text);
            if (s->page == AIGLASS_PAGE_FILES) s->rebuild = true;
        }
        break;
    case AG_READ_FILE:
        if (!strcmp(result->detail, "notes/focus.json")) {
            cJSON *state = ag_json_parse(result->text);
            cJSON *seconds = cJSON_GetObjectItemCaseSensitive(state, "seconds");
            cJSON *merit = cJSON_GetObjectItemCaseSensitive(state, "merit");
            if (cJSON_IsNumber(seconds) && seconds->valuedouble >= 0 && seconds->valuedouble <= UINT32_MAX)
                s->focus_seconds = (uint32_t)seconds->valuedouble;
            if (cJSON_IsNumber(merit) && merit->valuedouble >= 0 && merit->valuedouble <= UINT32_MAX)
                s->merit = (uint32_t)merit->valuedouble;
            cJSON_Delete(state);
        } else {
            char current_note[160];
            snprintf(current_note, sizeof(current_note), "notes/%s", s->note_name);
            if (!s->note_ready && !strcmp(result->detail, current_note)) {
                copy_text(s->note, sizeof(s->note), result->text);
                s->note_ready = true;
            }
            if (!strcmp(result->detail, s->selected_file)) copy_text(s->file_preview, sizeof(s->file_preview), result->text);
            if (s->page == AIGLASS_PAGE_FILES || s->page == AIGLASS_PAGE_NOTES) s->rebuild = true;
        }
        break;
    case AG_DELETE_FILE:
        s->selected_file[0] = s->file_preview[0] = '\0';
        s->delete_confirm = false;
        submit(s, AG_LIST_FILES, "", s->folder);
        if (s->page == AIGLASS_PAGE_FILES) s->rebuild = true;
        break;
    case AG_BROWSE:
        copy_text(s->browser, sizeof(s->browser), result->text);
        if (s->page == AIGLASS_PAGE_BROWSER) s->rebuild = true;
        break;
    case AG_AUDIO_START: case AG_VIDEO_START: case AG_PLAY_AUDIO:
        s->record_started = lv_tick_get(); break;
    case AG_PHONE_MESSAGE:
        copy_text(s->phone_message, sizeof(s->phone_message), result->text);
        if (!strcmp(result->detail, "chat") || !strcmp(result->detail, "translate")) {
            copy_text(s->draft, sizeof(s->draft), result->text);
            navigate(s, !strcmp(result->detail, "chat") ? AIGLASS_PAGE_CHAT : AIGLASS_PAGE_TRANSLATE, true);
            message(s, "已接收文字，点击发送");
            return;
        }
        if (s->page == AIGLASS_PAGE_PHONE) s->rebuild = true;
        if (s->page == AIGLASS_PAGE_FILES && strchr(result->detail, '/')) {
            s->selected_file[0] = s->file_preview[0] = '\0';
            if (!s->pending_id) submit(s, AG_LIST_FILES, "", s->folder);
        }
        break;
    default: break;
    }
    if (!s->pending_id) message(s, result->operation == AG_PHONE_MESSAGE ? "手机内容已更新" : "完成");
}

static void update_status(ui_state_t *s)
{
    ag_status_t status;
    char text[512], battery[16];
    time_t now = time(NULL);
    struct tm local;
    ag_services_status(s->services, &status);
    if (localtime_r(&now, &local)) {
        snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
        lv_label_set_text(s->clock, text);
    }
    if (status.battery_percent < 0) copy_text(battery, sizeof(battery), "--");
    else snprintf(battery, sizeof(battery), "%d%%", status.battery_percent);
    snprintf(text, sizeof(text), "%s  %s  %s", status.address[0] ? "联网" : "离线",
             status.phone_connected ? "手机" : "--", battery);
    lv_label_set_text(s->status, text);
    if (!s->dynamic) return;
    switch (s->page) {
    case AIGLASS_PAGE_PHONE:
        snprintf(text, sizeof(text), "%s\n%s\n收 %llu KB / 发 %llu KB", status.phone_connected ? "手机已连接" : "等待手机连接",
                 status.address[0] ? status.address : "网络未连接",
                 (unsigned long long)(status.bytes_received / 1024), (unsigned long long)(status.bytes_sent / 1024));
        break;
    case AIGLASS_PAGE_RECORDER: case AIGLASS_PAGE_CAMERA: {
        if (!status.hardware_available) copy_text(text, sizeof(text), "相机与麦克风不可用");
        else {
            unsigned elapsed = status.media_active ? (lv_tick_get() - s->record_started) / 1000 : 0;
            snprintf(text, sizeof(text), "%s\n%02u:%02u", status.media_active ? "媒体任务进行中" : "准备就绪", elapsed / 60, elapsed % 60);
        }
        break;
    }
    case AIGLASS_PAGE_SETTINGS:
        if (!status.hardware_available) copy_text(text, sizeof(text), "当前显示后端不支持背光调节");
        else snprintf(text, sizeof(text), "亮度设置 %d%%\n设备应用结果见下方状态", s->brightness);
        break;
    case AIGLASS_PAGE_FOCUS: {
        unsigned seconds = focus_time(s);
        snprintf(text, sizeof(text), "专注 %02u:%02u:%02u\n木鱼 %u 次\n%s", seconds / 3600, seconds / 60 % 60, seconds % 60,
                 s->merit, s->focus_running ? "打坐中" : "本地进度");
        break;
    }
    case AIGLASS_PAGE_NAVIGATION:
        snprintf(text, sizeof(text), "%s\n\n%s", status.phone_connected ? "手机导航" : "手机未连接",
                 status.phone_connected && status.navigation[0] ? status.navigation : "等待手机发送路线指引");
        break;
    case AIGLASS_PAGE_RIDE:
        if (status.phone_connected) snprintf(text, sizeof(text), "骑行\n\n%.1f km/h\n%.2f km", status.speed_kmh, status.distance_km);
        else copy_text(text, sizeof(text), "骑行\n\n等待手机发送速度与里程");
        break;
    default: return;
    }
    lv_label_set_text(s->dynamic, text);
}

void aiglass_home_key(lv_obj_t *view, uint32_t key)
{
    ui_state_t *s = view ? lv_obj_get_user_data(view) : NULL;
    if (!s) return;
    if (key == LV_KEY_ESC) { back(s); return; }
    if (!s->modal && s->page == AIGLASS_PAGE_GAME &&
        (key == LV_KEY_ENTER || key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP)) {
        aiglass_game_key(s->game, key); return;
    }
    lv_obj_t *focused = lv_group_get_focused(s->group);
    if (key == LV_KEY_NEXT) lv_group_focus_next(s->group);
    else if (key == LV_KEY_PREV) lv_group_focus_prev(s->group);
    else if (focused == s->output_scroll && (key == LV_KEY_UP || key == LV_KEY_DOWN))
        lv_obj_scroll_by(focused, 0, key == LV_KEY_UP ? 36 : -36, LV_ANIM_OFF);
    else if (focused && (lv_obj_check_type(focused, &lv_slider_class) ||
                        lv_obj_check_type(focused, &lv_keyboard_class) || lv_obj_check_type(focused, &lv_textarea_class)))
        lv_group_send_data(s->group, key);
    else if (key == LV_KEY_ENTER && focused) lv_event_send(focused, LV_EVENT_CLICKED, NULL);
    else if (key == LV_KEY_UP || key == LV_KEY_LEFT) lv_group_focus_prev(s->group);
    else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) lv_group_focus_next(s->group);
}

static void read_keys(ui_state_t *s)
{
    struct input_event event;
    if (s->input_fd < 0) return;
    for (unsigned n = 0; n < 32 && read(s->input_fd, &event, sizeof(event)) == (ssize_t)sizeof(event); ++n) {
        if (event.type != EV_KEY || !event.value) continue;
        uint32_t key = 0;
        switch (event.code) {
        case KEY_UP: key = LV_KEY_UP; break;
        case KEY_DOWN: key = LV_KEY_DOWN; break;
        case KEY_LEFT: key = LV_KEY_LEFT; break;
        case KEY_RIGHT: key = LV_KEY_RIGHT; break;
        case KEY_ENTER: case KEY_OK: if (event.value == 1) key = LV_KEY_ENTER; break;
        case KEY_ESC: case KEY_BACK: if (event.value == 1) key = LV_KEY_ESC; break;
        case KEY_VOLUMEUP: key = LV_KEY_PREV; break;
        case KEY_VOLUMEDOWN: case KEY_TAB: key = LV_KEY_NEXT; break;
        default: break;
        }
        if (key) aiglass_home_key(s->root, key);
    }
}

static void tick(lv_timer_t *timer)
{
    ui_state_t *s = timer->user_data;
    ag_result_t result;
    read_keys(s);
    ag_status_t connection;
    ag_services_status(s->services, &connection);
    if (connection.phone_paired != s->phone_paired) {
        s->phone_paired = connection.phone_paired;
        s->unpair_confirm = false;
        if (!connection.phone_paired || s->page == AIGLASS_PAGE_PAIRING) {
            s->close_editor = true;
            navigate(s, connection.phone_paired ? AIGLASS_PAGE_MENU : AIGLASS_PAGE_PAIRING, false);
        }
    }
    if (connection.message_sequence != s->message_sequence) {
        s->message_sequence = connection.message_sequence;
        lv_label_set_text(s->notification, connection.last_message[0] ? connection.last_message : "尚未收到手机消息");
    }
    for (unsigned i = 0; i < 4 && ag_services_poll(s->services, &result); ++i) receive_result(s, &result);
    if (s->rebuild && (!s->modal || s->close_editor)) render_page(s);
    uint32_t now = lv_tick_get();
    if (now / 1000 != s->last_second) { update_status(s); s->last_second = now / 1000; }
    if (s->stop_after_cancel && !s->pending_id) {
        ag_status_t status;
        ag_services_status(s->services, &status);
        s->stop_after_cancel = false;
        if (status.media_active) submit(s, AG_MEDIA_STOP, "", "");
    }
    if (s->services && !s->pending_id) {
        if (s->page == AIGLASS_PAGE_NOTES && !s->note_loaded) {
            char path[160];
            snprintf(path, sizeof(path), "notes/%s", s->note_name);
            s->note_loaded = submit(s, AG_READ_FILE, "", path) != 0;
        } else if (s->page == AIGLASS_PAGE_FOCUS && !s->focus_loaded)
            s->focus_loaded = submit(s, AG_READ_FILE, "", "notes/focus.json") != 0;
    }
    if (s->live_translation && s->page == AIGLASS_PAGE_TRANSLATE && !s->pending_id && (int32_t)(now - s->live_next) >= 0)
        if (!submit(s, AG_VOICE_TRANSLATE, "", language_codes[s->language])) s->live_translation = false;
    if (s->settings_dirty && !s->pending_id && (int32_t)(now - s->settings_due) >= 0) {
        char number[16];
        snprintf(number, sizeof(number), "%d", s->brightness);
        if (submit(s, AG_BRIGHTNESS, number, "")) s->settings_dirty = 0;
    }
}

static void destroy(lv_event_t *event)
{
    ui_state_t *s = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != s->root) return;
    if (s->timer) lv_timer_del(s->timer);
    if (s->input_fd >= 0) close(s->input_fd);
    if (s->group) lv_group_del(s->group);
    if (s->owns_services) ag_services_close(s->services);
    else if (s->pending_id) ag_services_cancel(s->services, s->pending_id);
    free(s);
}

lv_obj_t *aiglass_home_create_with_config(lv_obj_t *parent, const aiglass_home_config_t *config)
{
    ui_state_t *s;
    if (!parent) return NULL;
    lv_coord_t width = config && config->width ? config->width : AIGLASS_HOME_WIDTH;
    lv_coord_t height = config && config->height ? config->height : AIGLASS_HOME_HEIGHT;
    if (width < 640 || height < 480 || width > 2048 || height > 2048) { errno = EINVAL; return NULL; }
    s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->input_fd = -1;
    s->width = width; s->height = height;
    s->font = config && config->font ? config->font : &aiglass_font_16;
    if (!s->font->line_height || s->font->line_height > 22) { free(s); errno = EINVAL; return NULL; }
    s->group = lv_group_create();
    if (!s->group) { free(s); return NULL; }
    s->owns_services = !config || !config->services;
    s->services = s->owns_services ? ag_services_open(config ? config->services_config : NULL) : config->services;
    if (!s->services) s->service_error = errno;
    ag_status_t initial;
    ag_services_status(s->services, &initial);
    s->hardware_available = initial.hardware_available;
    s->phone_paired = initial.phone_paired;
    s->page = s->services && !initial.phone_paired ? AIGLASS_PAGE_PAIRING : AIGLASS_PAGE_MENU;
    const char *input_device = config && config->input_device ? config->input_device : getenv("AIGLASS_INPUT_DEVICE");
    if (input_device && *input_device) s->input_fd = open(input_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    copy_text(s->folder, sizeof(s->folder), "inbox");
    copy_text(s->note_name, sizeof(s->note_name), "note.txt");
    copy_text(s->download_name, sizeof(s->download_name), "download.bin");
    s->brightness = 50;
    s->root = box(parent, 0, 0, width, height);
    lv_obj_center(s->root);
    lv_obj_set_style_bg_color(s->root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s->root, LV_OPA_COVER, 0);
    lv_obj_set_user_data(s->root, s);
    lv_obj_add_event_cb(s->root, destroy, LV_EVENT_DELETE, s);
    s->clock = label(s, s->root, "--:--", 12, 10, 64, 24);
    s->title = label(s, s->root, "", 96, 10, width - 320, 24);
    s->status = label(s, s->root, "", width - 200, 10, 188, 24);
    lv_obj_set_style_text_align(s->status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *line = box(s->root, 8, UI_HEADER - 2, width - 16, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(DIM_GREEN), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    s->body = box(s->root, UI_MARGIN, UI_HEADER + UI_MARGIN,
                  width - 2 * UI_MARGIN, height - UI_HEADER - UI_FOOTER - 2 * UI_MARGIN);
    s->back = button(s, s->root, "返回", 8, height - UI_FOOTER, 56, 24, ACT_BACK, 0, NULL);
    s->footer = label(s, s->root, "", 80, height - UI_FOOTER + 2, width - 92, 22);
    lv_obj_t *message_line = box(s->root, 12, height - 56, width - 24, 1);
    lv_obj_set_style_bg_color(message_line, lv_color_hex(DIM_GREEN), 0);
    lv_obj_set_style_bg_opa(message_line, LV_OPA_COVER, 0);
    s->notification = label(s, s->root, initial.last_message[0] ? initial.last_message : "尚未收到手机消息",
                            12, height - 50, width - 24, 48);
    render_page(s);
    update_status(s);
    if (s->service_error) message(s, strerror(s->service_error));
    else message(s, "选择应用 / 确认进入");
    s->timer = lv_timer_create(tick, 50, s);
    if (!s->timer) { lv_obj_del(s->root); return NULL; }
    return s->root;
}

lv_obj_t *aiglass_home_create(lv_obj_t *parent)
{
    return aiglass_home_create_with_config(parent, NULL);
}

lv_obj_t *aiglass_home_show(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    if (!aiglass_home_create(screen)) { lv_obj_del(screen); return NULL; }
    lv_scr_load(screen);
    return screen;
}

void aiglass_home_open(lv_obj_t *view, aiglass_page_t page)
{
    ui_state_t *s = view ? lv_obj_get_user_data(view) : NULL;
    if (s) navigate(s, page, true);
}

lv_group_t *aiglass_home_group(lv_obj_t *view)
{
    ui_state_t *s = view ? lv_obj_get_user_data(view) : NULL;
    return s ? s->group : NULL;
}

aiglass_page_t aiglass_home_page(lv_obj_t *view)
{
    ui_state_t *s = view ? lv_obj_get_user_data(view) : NULL;
    return s ? s->page : AIGLASS_PAGE_MENU;
}
