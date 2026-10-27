#define _GNU_SOURCE
#include "aiglass_home.h"
#include "aiglass_internal.h"
#include "aiglass_json.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static int width = AIGLASS_HOME_WIDTH, height = AIGLASS_HOME_HEIGHT, pointer_x, pointer_y;
static bool dirty, pointer_down;
static uint64_t release_at;
static unsigned char *pixels;
static lv_obj_t *view;
static ag_services_t *services;
static void stop(int sig) { (void)sig; running = 0; }

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors)
{
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++, colors++) {
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            size_t i = ((size_t)y * width + x) * 3;
            pixels[i] = colors->ch.red;
            pixels[i + 1] = colors->ch.green;
            pixels[i + 2] = colors->ch.blue;
        }
    dirty = true;
    lv_disp_flush_ready(driver);
}

static void pointer_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    data->point.x = pointer_x;
    data->point.y = pointer_y;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static int save_frame(void)
{
    FILE *f = fopen("frame.ppm.part", "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    size_t size = (size_t)width * height * 3;
    bool ok = fwrite(pixels, 1, size, f) == size;
    if (fclose(f)) ok = false;
    if (ok && !rename("frame.ppm.part", "frame.ppm")) { dirty = false; return 0; }
    return -1;
}

static const char *str(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static lv_obj_t *find_button(lv_obj_t *root, const char *text)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) {
        lv_obj_t *parent = lv_obj_get_parent(root);
        while (parent && !lv_obj_check_type(parent, &lv_btn_class)) parent = lv_obj_get_parent(parent);
        if (parent) return parent;
    }
    /* Prefer the topmost controls, and ignore status labels with the same text. */
    for (int32_t i = (int32_t)lv_obj_get_child_cnt(root) - 1; i >= 0; i--) {
        lv_obj_t *found = find_button(lv_obj_get_child(root, i), text);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *find_editor(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_textarea_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); i++) {
        lv_obj_t *found = find_editor(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}

static void inspect(lv_obj_t *root, cJSON *list)
{
    if (lv_obj_check_type(root, &lv_label_class)) cJSON_AddItemToArray(list, cJSON_CreateString(lv_label_get_text(root)));
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); i++) inspect(lv_obj_get_child(root, i), list);
}

static int control(cJSON *request)
{
    const char *op = str(request, "op");
    if (!strcmp(op, "inspect")) return 0;
    if (!strcmp(op, "open")) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(request, "page");
        if (!cJSON_IsNumber(p) || p->valuedouble != p->valueint || p->valueint < 0 || p->valueint > AIGLASS_PAGE_PAIRING) return -EINVAL;
        aiglass_home_open(view, p->valueint);
    } else if (!strcmp(op, "key")) {
        const char *name = str(request, "key");
        uint32_t key = !strcmp(name, "Enter") ? LV_KEY_ENTER : !strcmp(name, "Escape") ? LV_KEY_ESC :
            !strcmp(name, "Tab") ? LV_KEY_NEXT : !strcmp(name, "ArrowUp") ? LV_KEY_UP :
            !strcmp(name, "ArrowDown") ? LV_KEY_DOWN : !strcmp(name, "ArrowLeft") ? LV_KEY_LEFT :
            !strcmp(name, "ArrowRight") ? LV_KEY_RIGHT : 0;
        if (!key) return -EINVAL;
        aiglass_home_key(view, key);
    } else if (!strcmp(op, "tap")) {
        cJSON *x = cJSON_GetObjectItemCaseSensitive(request, "x"), *y = cJSON_GetObjectItemCaseSensitive(request, "y");
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || x->valuedouble < 0 || y->valuedouble < 0 || x->valuedouble >= width || y->valuedouble >= height) return -EINVAL;
        pointer_x = x->valueint;
        pointer_y = y->valueint;
        pointer_down = true;
        release_at = ag_milliseconds() + 90;
    } else if (!strcmp(op, "click")) {
        lv_obj_t *object = find_button(view, str(request, "text"));
        if (!object) return -ENOENT;
        if (lv_obj_has_state(object, LV_STATE_DISABLED)) return -ENOTSUP;
        lv_event_send(object, LV_EVENT_CLICKED, NULL);
    } else if (!strcmp(op, "edit")) {
        lv_obj_t *editor = find_editor(view);
        if (!editor || strlen(str(request, "text")) >= AG_INPUT_MAX) return -EINVAL;
        lv_textarea_set_text(editor, str(request, "text"));
    } else return -ENOTSUP;
    return 0;
}

static void handle_client(int server)
{
    int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0) return;
    struct timeval timeout = {0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char buffer[16384];
    size_t used = 0;
    while (used < sizeof(buffer) - 1) {
        ssize_t n = recv(fd, buffer + used, sizeof(buffer) - used - 1, 0);
        if (n <= 0) break;
        used += (size_t)n;
        if (memchr(buffer, '\n', used)) break;
    }
    buffer[used] = '\0';
    cJSON *request = ag_json_parse(buffer), *reply = cJSON_CreateObject();
    int rc = cJSON_IsObject(request) ? control(request) : -EINVAL;
    cJSON_AddBoolToObject(reply, "ok", !rc);
    cJSON_AddNumberToObject(reply, "code", rc);
    cJSON_AddNumberToObject(reply, "width", width);
    cJSON_AddNumberToObject(reply, "height", height);
    cJSON_AddNumberToObject(reply, "page", aiglass_home_page(view));
    ag_status_t state;
    ag_services_status(services, &state);
    cJSON_AddStringToObject(reply, "last_message", state.last_message);
    cJSON_AddNumberToObject(reply, "message_sequence", (double)state.message_sequence);
    cJSON_AddBoolToObject(reply, "paired", state.phone_paired);
    if (rc) cJSON_AddStringToObject(reply, "error", strerror(-rc));
    cJSON *labels = cJSON_AddArrayToObject(reply, "labels");
    inspect(view, labels);
    char *json = cJSON_PrintUnformatted(reply);
    if (json) { send(fd, json, strlen(json), MSG_NOSIGNAL); send(fd, "\n", 1, MSG_NOSIGNAL); free(json); }
    cJSON_Delete(request);
    cJSON_Delete(reply);
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc == 3) { width = atoi(argv[1]); height = atoi(argv[2]); }
    if (width < 640 || height < 480 || width > 1024 || height > 1024) return 2;
    umask(0077);
    int lock = open("host.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("host already running"); return 1; }
    /* Holding this runtime's lock makes cleanup of its stale sockets safe. */
    unlink("ui.sock"); unlink("data/bridge.sock");
    ag_services_config_t config;
    ag_services_config_init(&config);
    config.hardware_access = false;
    ag_copy(config.data_dir, sizeof(config.data_dir), "./data");
    config.socket_path[0] = '\0';
    config.phone_port = 0;
    config.backlight[0] = '\0';
    services = ag_services_open(&config);
    if (!services) { perror("services"); return 1; }
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX}; strcpy(address.sun_path, "ui.sock");
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) || listen(server, 4)) { perror("ui socket"); ag_services_close(services); return 1; }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    lv_init();
    pixels = calloc((size_t)width * height, 3);
    lv_color_t *buffer = calloc((size_t)width * 40, sizeof(*buffer));
    if (!pixels || !buffer) return 1;
    lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, buffer, NULL, width * 40);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = width;
    display.ver_res = height;
    display.draw_buf = &draw;
    display.flush_cb = flush;
    lv_disp_t *disp = lv_disp_drv_register(&display);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = pointer_read;
    lv_indev_t *pointer = lv_indev_drv_register(&input);
    aiglass_home_config_t ui = {.width = width, .height = height, .services = services, .input_device = ""};
    view = aiglass_home_create_with_config(lv_scr_act(), &ui);
    if (!view) { fprintf(stderr, "UI creation failed\n"); return 1; }
    fprintf(stdout, "LVGL %d.%d.%d host %dx%d ready; data=./data\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, width, height); fflush(stdout);
    uint64_t last = ag_milliseconds(), saved = 0;
    while (running) {
        uint64_t now = ag_milliseconds();
        lv_tick_inc((uint32_t)(now - last)); last = now;
        if (pointer_down && now >= release_at) pointer_down = false;
        handle_client(server);
        lv_timer_handler();
        if (dirty && now - saved >= 100) { if (save_frame()) perror("frame"); saved = now; }
        usleep(5000);
    }
    lv_obj_del(view);
    lv_indev_delete(pointer);
    lv_disp_remove(disp);
    /* LVGL 8.3 leaves the driver's draw context owned by the caller. */
    display.draw_ctx_deinit(&display, display.draw_ctx);
    lv_mem_free(display.draw_ctx);
    ag_services_close(services);
    close(server);
    unlink("ui.sock");
    close(lock);
    free(buffer);
    free(pixels);
    return 0;
}
