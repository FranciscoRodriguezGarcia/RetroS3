#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct ui_animation ui_animation_t;
ui_animation_t *ui_animation_load(const char *path);
void ui_animation_free(ui_animation_t *animation);
void ui_animation_restart(ui_animation_t *animation);
bool ui_animation_tick(ui_animation_t *animation, int x, int y);
int ui_animation_width(const ui_animation_t *animation);
int ui_animation_height(const ui_animation_t *animation);
void ui_play_startup(void);
