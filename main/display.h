#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t display_init(void);
/* Fill a packed RGB565 block in wire byte order. Called synchronously;
 * destination is valid only during the callback. Draw waits for final DMA. */
typedef void (*display_block_fill_t)(void *context, uint16_t *destination,
                                     int first_line, int lines, int width);
esp_err_t display_draw_rgb565_blocks(int x, int y, int width, int height,
                                     display_block_fill_t fill, void *context);
void display_clear(void);

void display_fill_rect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
);

esp_err_t display_draw_rgb565(
    int x,
    int y,
    int width,
    int height,
    const uint16_t *pixels
);
void display_draw_text(
    int x,
    int y,
    const char *text,
    uint16_t foreground,
    uint16_t background
);

esp_err_t display_draw_rgb565_scaled_3_2(
    int x,
    int y,
    const uint16_t *src,
    int src_width,
    int src_height
);
