#include "retro_audio.h"
#include "sdkconfig.h"
#include "nes.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nofrendo.h"
#include "osd.h"
#include <stdint.h>
#include <stdlib.h>
#include "input.h"
#include "event.h"
#include "nesinput.h"
#include "esp_heap_caps.h"
#include "bitmap.h"
#include "vid_drv.h"
#include "osd.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

#define NES_WIDTH 256
#define NES_HEIGHT 240
#define NES_DISPLAY_HEIGHT 224
#define NES_CROP_TOP 8
#include "nofrendo.h"

#include <stdio.h>
#include <string.h>
#include <osd.h>
#include <nofconfig.h>

static char nes_config_filename[] = "/sdcard/nofrendo.cfg";
/* RGB565 in display wire byte order, ready for direct DMA buffer writes. */
static uint16_t nes_palette[256];
static bitmap_t *nes_screen = NULL;
static uint8_t *nes_screen_data = NULL;
static unsigned char *nes_rom_data = NULL;

void osd_fullname(char *fullname, const char *shortname)
{
    if (!fullname || !shortname)
        return;

    snprintf(fullname, PATH_MAX, "%s", shortname);
}
char *osd_getromdata(void)
{
    return (char *)nes_rom_data;
}
char *osd_newextension(char *string, char *ext)
{
    (void)ext;
    return string;
}

int osd_makesnapname(char *filename, int len)
{
    (void)filename;
    (void)len;
    return -1;
}


int osd_main(int argc, char *argv[])
{
    if (argc < 2 || argv[1] == NULL)
        return -1;

    const char *rom_path = argv[1];

    FILE *file = fopen(rom_path, "rb");
    if (file == NULL)
    {
        printf("NES: no se pudo abrir %s\n", rom_path);
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return -1;
    }

    long file_size = ftell(file);

    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }

    nes_rom_data = heap_caps_malloc(
        (size_t)file_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (nes_rom_data == NULL)
    {
        printf("NES: memoria insuficiente para ROM\n");
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(
        nes_rom_data,
        1,
        (size_t)file_size,
        file
    );

    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        printf("NES: error leyendo ROM\n");
        free(nes_rom_data);
        nes_rom_data = NULL;
        return -1;
    }

    printf("NES: ROM cargada (%ld bytes)\n", file_size);

    config.filename = nes_config_filename;

    int result = main_loop(rom_path, system_nes);

    free(nes_rom_data);
    nes_rom_data = NULL;

    return result;
}



static int nes_video_init(int width, int height)
{
    if (width != NES_WIDTH || height != NES_HEIGHT)
        return -1;

    printf("NES VIDEO INIT: block DMA, internal_free=%u, internal_largest=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return 0;
}

static void nes_video_shutdown(void)
{
    if (nes_screen)
        bmp_destroy(&nes_screen);

    free(nes_screen_data);
    nes_screen_data = NULL;

}

static int nes_video_setmode(int width, int height)
{
    return (width == NES_WIDTH && height == NES_HEIGHT)
        ? 0 : -1;
}

static void nes_video_palette(rgb_t *palette)
{
    for (int i = 0; i < 256; i++) {
        uint16_t r = (uint16_t)(palette[i].r >> 3);
        uint16_t g = (uint16_t)(palette[i].g >> 2);
        uint16_t b = (uint16_t)(palette[i].b >> 3);

        nes_palette[i] = __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | b));
    }
}


static unsigned int video_blit_count;

unsigned int osd_get_video_blit_count(void)
{
    return video_blit_count;
}

typedef struct {
    const bitmap_t *bitmap;
    int64_t conversion_us;
} nes_block_context_t;

static void nes_convert_block(void *context, uint16_t *destination,
                              int first_line, int lines, int width)
{
    nes_block_context_t *block = context;
    int64_t start_us = esp_timer_get_time();
    for (int y = 0; y < lines; y++)
    {
        const uint8_t *source = block->bitmap->line[first_line + y];
        for (int x = 0; x < width; x++)
            destination[y * width + x] = nes_palette[source[x]];
    }
    block->conversion_us += esp_timer_get_time() - start_us;
}

static void nes_video_blit(bitmap_t *bitmap,
                           int num_dirties,
                           rect_t *dirty_rects)
{
    video_blit_count++;
    (void)num_dirties;
    (void)dirty_rects;

    static uint32_t debug_frames = 0;
    static uint32_t sample_count = 0;

    static int64_t total_conversion_us = 0;
    static int64_t total_spi_us = 0;

    debug_frames++;

    if (!bitmap ||
        bitmap->width < NES_WIDTH ||
        bitmap->height < NES_DISPLAY_HEIGHT)
    {
        if (debug_frames % 60 == 0)
        {
            printf("NES VIDEO: framebuffer invalido\n");
        }

        return;
    }

    // Inicio de medición
    int64_t start_us = esp_timer_get_time();

static bool nes_task_logged = false;

if (!nes_task_logged)
{
    printf(
        "NES VIDEO TASK: name=%s priority=%u core=%d\n",
        pcTaskGetName(NULL),
        (unsigned)uxTaskPriorityGet(NULL),
        xPortGetCoreID()
    );

    nes_task_logged = true;
}
    // Transferir framebuffer a ST7789
    nes_block_context_t block = { .bitmap = bitmap, .conversion_us = 0 };
    esp_err_t err = display_draw_rgb565_blocks(
        32, 8, NES_WIDTH, NES_DISPLAY_HEIGHT, nes_convert_block, &block);

    int64_t end_us = esp_timer_get_time();

    // Acumular tiempos
    total_conversion_us += block.conversion_us;
    /* Conversion overlaps DMA: the remainder is not physical SPI time. */
    total_spi_us += end_us - start_us - block.conversion_us;
    sample_count++;

    // Mostrar promedios cada 60 frames dibujados
    if (sample_count >= 60)
    {
        double conversion_ms =
            (double)total_conversion_us / sample_count / 1000.0;

        double spi_ms =
            (double)total_spi_us / sample_count / 1000.0;

        printf(
            "NES PERF: conversion=%.2f ms | other_wait=%.2f ms | total=%.2f ms | draw=%s\n",
            conversion_ms,
            spi_ms,
            conversion_ms + spi_ms,
            esp_err_to_name(err)
        );

        sample_count = 0;
        total_conversion_us = 0;
        total_spi_us = 0;
    }
}

     


static bitmap_t *nes_video_lock(void)
{
    if (nes_screen)
        return nes_screen;

    nes_screen_data = calloc(NES_WIDTH * NES_HEIGHT, 1);
    if (!nes_screen_data)
        return NULL;

    nes_screen = bmp_createhw(
        nes_screen_data,
        NES_WIDTH,
        NES_HEIGHT,
        NES_WIDTH
    );

    if (!nes_screen) {
        free(nes_screen_data);
        nes_screen_data = NULL;
    }

    return nes_screen;
}

static void nes_video_unlock(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
}

static viddriver_t nes_video_driver = {
    .name = "Retro53 ST7789",
    .init = nes_video_init,
    .shutdown = nes_video_shutdown,
    .set_mode = nes_video_setmode,
    .set_palette = nes_video_palette,
    .clear = NULL,
    .lock_write = nes_video_lock,
    .free_write = nes_video_unlock,
    .custom_blit = nes_video_blit,
    .invalidate = false
};

void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width = NES_WIDTH;
    info->default_height = NES_HEIGHT;
    info->driver = &nes_video_driver;
}




static esp_timer_handle_t nes_timer = NULL;

static void nes_timer_callback(void *arg)
{
    (void)arg;
    nofrendo_ticks++;
}

int osd_installtimer(
    int frequency,
    void *func,
    int funcsize,
    void *counter,
    int countersize)
{
    (void)func;
    (void)funcsize;
    (void)counter;
    (void)countersize;

    if (frequency <= 0)
        return -1;

    if (nes_timer != NULL)
        return 0;

    const esp_timer_create_args_t timer_args = {
        .callback = nes_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "nes_timer"
    };

    esp_err_t err = esp_timer_create(&timer_args, &nes_timer);

    if (err != ESP_OK)
        return -1;

    nofrendo_ticks = 0;

    err = esp_timer_start_periodic(
        nes_timer,
        1000000ULL / (uint64_t)frequency
    );

    if (err != ESP_OK) {
        esp_timer_delete(nes_timer);
        nes_timer = NULL;
        return -1;
    }

    return 0;
}

static void (*nes_audio_callback)(void *, int) = NULL;

void osd_setsound(void (*playfunc)(void *, int))
{
    nes_audio_callback = playfunc;
}

void osd_audio_frame(void)
{
    static int16_t pcm[(RETRO_AUDIO_RATE + NES_REFRESH_RATE - 1) / NES_REFRESH_RATE];
    static unsigned remainder;
    remainder += RETRO_AUDIO_RATE;
    unsigned count = remainder / NES_REFRESH_RATE;
    remainder %= NES_REFRESH_RATE;
    if (nes_audio_callback) {
        nes_audio_callback(pcm, count);
#if CONFIG_RETRO_USB_AUDIO_NES
        retro_audio_submit(pcm, count);
#endif
    }
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = RETRO_AUDIO_RATE;
    info->bps = 16;
}

int osd_init(void)
{
    return 0;
}

void osd_shutdown(void)
{
    if (nes_timer != NULL) {
        esp_timer_stop(nes_timer);
        esp_timer_delete(nes_timer);
        nes_timer = NULL;
    }

    nes_audio_callback = NULL;
}

void osd_getmouse(int *x, int *y, int *button)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (button) *button = 0;
}

void osd_getinput(void)
{
    if (input_consume_menu_request()) esp_restart();
    static input_state_t previous = 0;

    input_state_t current = input_get_state();
    input_state_t changed = current ^ previous;
if (changed)
{
    /* printf("NES INPUT: current=0x%08lX changed=0x%08lX\n",
           (unsigned long)current,
           (unsigned long)changed); */
}
    static const struct {
        input_state_t button;
        int event;
    } mapping[] = {
        { INPUT_BUTTON_A,      event_joypad1_a },
        { INPUT_BUTTON_B,      event_joypad1_b },
        { INPUT_BUTTON_SELECT, event_joypad1_select },
        { INPUT_BUTTON_START,  event_joypad1_start },
        { INPUT_BUTTON_UP,     event_joypad1_up },
        { INPUT_BUTTON_DOWN,   event_joypad1_down },
        { INPUT_BUTTON_LEFT,   event_joypad1_left },
        { INPUT_BUTTON_RIGHT,  event_joypad1_right }
    };

    for (size_t i = 0; i < sizeof(mapping) / sizeof(mapping[0]); i++)
    {
        if (changed & mapping[i].button)
        {
            event_t handler = event_get(mapping[i].event);

            if (handler)
            {
                handler(
                    (current & mapping[i].button)
                        ? INP_STATE_MAKE
                        : INP_STATE_BREAK
                );
            }
        }
    }

    previous = current;
}
