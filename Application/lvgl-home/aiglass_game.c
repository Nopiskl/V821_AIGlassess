#include "aiglass_game.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    lv_obj_t *root, *player, *enemy, *bullet, *score_label;
    lv_timer_t *timer;
    int x, y, enemy_x, enemy_y, bullet_x, bullet_y, velocity;
    int width, height;
    unsigned mode, score, rng, frames;
    bool paused, over, shot;
} game_t;

static unsigned random_value(game_t *g)
{
    g->rng = g->rng * 1664525U + 1013904223U;
    return g->rng;
}

static lv_obj_t *shape(lv_obj_t *parent, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, (lv_coord_t)width, (lv_coord_t)height);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(object, 2, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(0x42ff80), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
    return object;
}

static bool overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void spawn_enemy(game_t *g)
{
    if (g->mode == 2) { g->enemy_x = g->width - 18; g->enemy_y = g->height - 30; }
    else {
        g->enemy_x = 8 + (int)(random_value(g) % (unsigned)(g->width - 40));
        g->enemy_y = 32;
    }
}

static void render(game_t *g)
{
    char label[96];
    snprintf(label, sizeof(label), "得分 %u%s", g->score, g->over ? "  结束 / 确认重试" : g->paused ? "  已暂停" : "");
    lv_label_set_text(g->score_label, label);
    lv_obj_set_pos(g->player, (lv_coord_t)g->x, (lv_coord_t)g->y);
    lv_obj_set_pos(g->enemy, (lv_coord_t)g->enemy_x, (lv_coord_t)g->enemy_y);
    lv_obj_set_pos(g->bullet, (lv_coord_t)g->bullet_x, (lv_coord_t)g->bullet_y);
    if (g->shot) lv_obj_clear_flag(g->bullet, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g->bullet, LV_OBJ_FLAG_HIDDEN);
}

static void reset(game_t *g)
{
    g->score = g->frames = 0;
    g->paused = g->over = g->shot = false;
    g->velocity = 0;
    g->x = g->mode == 2 ? 24 : g->width / 2 - 10;
    g->y = g->height - 30;
    spawn_enemy(g);
    render(g);
}

static void tick(lv_timer_t *timer)
{
    game_t *g = timer->user_data;
    if (g->paused || g->over) return;
    ++g->frames;
    if (g->mode == 2) {
        g->enemy_x -= 5 + (int)(g->score / 10 > 5 ? 5 : g->score / 10);
        g->y += g->velocity;
        if (g->y < g->height - 30) ++g->velocity;
        else { g->y = g->height - 30; g->velocity = 0; }
        if (g->enemy_x < 0) { ++g->score; spawn_enemy(g); }
    } else if (g->mode == 3) {
        g->enemy_x += (g->frames / 40) % 2 ? -4 : 4;
        if (g->enemy_x < 8) g->enemy_x = 8;
        if (g->enemy_x > g->width - 28) g->enemy_x = g->width - 28;
    } else {
        g->enemy_y += 3 + (int)(g->score / 10 > 6 ? 6 : g->score / 10);
        if (g->enemy_y > g->height - 20) { ++g->score; spawn_enemy(g); }
    }
    if (g->shot) {
        g->bullet_y -= 9;
        if (g->bullet_y < 30) g->shot = false;
        if (overlaps(g->bullet_x, g->bullet_y, 4, 10, g->enemy_x, g->enemy_y, 20, 24)) {
            g->shot = false; g->score += 3; spawn_enemy(g);
        }
    }
    if (g->mode != 3 && overlaps(g->x, g->y, 20, 20, g->enemy_x, g->enemy_y, 20, 24)) g->over = true;
    render(g);
}

static void destroy(lv_event_t *event)
{
    game_t *g = lv_event_get_user_data(event);
    if (g->timer) lv_timer_del(g->timer);
    free(g);
}

lv_obj_t *aiglass_game_create(lv_obj_t *parent, unsigned mode, const lv_font_t *font)
{
    if (!parent || mode > 3) return NULL;
    game_t *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->root = lv_obj_create(parent);
    lv_obj_remove_style_all(g->root);
    lv_obj_set_size(g->root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(g->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(parent);
    g->width = lv_obj_get_content_width(parent);
    g->height = lv_obj_get_content_height(parent);
    if (g->width < 120 || g->height < 100) { lv_obj_del(g->root); free(g); return NULL; }
    g->mode = mode;
    g->rng = lv_tick_get() | 1U;
    lv_obj_set_user_data(g->root, g);
    lv_obj_add_event_cb(g->root, destroy, LV_EVENT_DELETE, g);
    g->score_label = lv_label_create(g->root);
    lv_obj_set_style_text_font(g->score_label, font, 0);
    lv_obj_set_style_text_color(g->score_label, lv_color_hex(0x42ff80), 0);
    lv_obj_set_width(g->score_label, (lv_coord_t)(g->width - 8));
    lv_obj_set_pos(g->score_label, 4, 4);
    g->player = shape(g->root, 20, 20);
    g->enemy = shape(g->root, 20, 24);
    g->bullet = shape(g->root, 4, 10);
    reset(g);
    g->timer = lv_timer_create(tick, 40, g);
    if (!g->timer) { lv_obj_del(g->root); return NULL; }
    return g->root;
}

void aiglass_game_key(lv_obj_t *game, uint32_t key)
{
    if (!game) return;
    game_t *g = lv_obj_get_user_data(game);
    if (!g) return;
    if (g->over) { if (key == LV_KEY_ENTER) reset(g); return; }
    if (g->paused) return;
    if (g->mode != 2) {
        if (key == LV_KEY_LEFT) g->x -= 16;
        if (key == LV_KEY_RIGHT) g->x += 16;
        if (g->x < 4) g->x = 4;
        if (g->x > g->width - 24) g->x = g->width - 24;
    }
    if (key == LV_KEY_ENTER || key == LV_KEY_UP) {
        if (g->mode == 2 && !g->velocity && g->y == g->height - 30) g->velocity = -13;
        else if ((g->mode == 0 || g->mode == 3) && !g->shot) {
            g->shot = true; g->bullet_x = g->x + 8; g->bullet_y = g->y - 10;
        }
    }
    render(g);
}

void aiglass_game_pause(lv_obj_t *game)
{
    if (!game) return;
    game_t *g = lv_obj_get_user_data(game);
    if (g) { g->paused = !g->paused; render(g); }
}
