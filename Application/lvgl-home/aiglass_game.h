#ifndef AIGLASS_GAME_H
#define AIGLASS_GAME_H
#include "lvgl.h"
/* 0: plane, 1: racing, 2: runner, 3: target shooting. */
lv_obj_t *aiglass_game_create(lv_obj_t *parent, unsigned mode, const lv_font_t *font);
void aiglass_game_key(lv_obj_t *game, uint32_t key);
void aiglass_game_pause(lv_obj_t *game);
#endif
