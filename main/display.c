#include "display.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
/* Two 16-line buffers use the same internal RAM as one 32-line buffer. */
#define DISPLAY_DMA_LINES 16

static uint16_t *draw_dma_buffer[2] = { NULL, NULL };

static const char *TAG = "DISPLAY";
static volatile int64_t last_dma_done_us = 0;
static esp_lcd_panel_handle_t g_panel = NULL;
static SemaphoreHandle_t lcd_done_sem = NULL;
static uint16_t *scale_dma_buffer = NULL;
static uint16_t *clear_dma_buffer = NULL;

static bool lcd_color_done_callback(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx
)
{
    BaseType_t high_task_wakeup = pdFALSE;
last_dma_done_us = esp_timer_get_time();
    xSemaphoreGiveFromISR(
        lcd_done_sem,
        &high_task_wakeup
    );

    return high_task_wakeup == pdTRUE;
}

//-------------------------------------------------------------
static esp_lcd_panel_handle_t iniciar_tft(void)
{
    ESP_LOGI(TAG, "Inicializando TFT...");

    spi_bus_config_t buscfg = {
        .sclk_io_num = TFT_SCK,
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        // Un frame RGB565 completo
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t)
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            TFT_HOST,
            &buscfg,
            SPI_DMA_CH_AUTO
        )
    );

    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
    .dc_gpio_num = TFT_DC,
    .cs_gpio_num = TFT_CS,
    .pclk_hz = 60 * 1000 * 1000,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .spi_mode = 0,
    .trans_queue_depth = 2,

    .on_color_trans_done = lcd_color_done_callback,
    .user_ctx = NULL,
};

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)TFT_HOST,
            &io_config,
            &io_handle
        )
    );

    esp_lcd_panel_handle_t panel = NULL;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB
    };



    ESP_ERROR_CHECK(
        esp_lcd_new_panel_st7789(
            io_handle,
            &panel_config,
            &panel
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_reset(panel)
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_init(panel)
    );

    /*
     * Tu panel físico es 240x320 pero lo queremos horizontal.
     * Equivale conceptualmente al setRotation(1) de Adafruit.
     */
    ESP_ERROR_CHECK(
        esp_lcd_panel_swap_xy(panel, true)
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_mirror(panel, false, true)
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(panel, true)
    );

    ESP_LOGI(TAG, "TFT OK");

    lcd_done_sem = xSemaphoreCreateBinary();

if (lcd_done_sem == NULL)
{
    ESP_LOGE(TAG, "No pude crear lcd_done_sem");
    return NULL;
}

clear_dma_buffer = heap_caps_malloc(
    TFT_STRIPE_PIXELS * sizeof(uint16_t),
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
);

if (clear_dma_buffer == NULL)
{
    ESP_LOGE(TAG, "No pude reservar clear DMA buffer");
    return NULL;
}

    return panel;
}
// ------------------------------------------------------------

static void limpiar_tft(void)
{
    memset(
        clear_dma_buffer,
        0,
        TFT_STRIPE_PIXELS * sizeof(uint16_t)
    );

    for (int y = 0; y < TFT_HEIGHT; y += TFT_STRIPE_LINES)
    {
        int lines = TFT_STRIPE_LINES;

        if (y + lines > TFT_HEIGHT)
        {
            lines = TFT_HEIGHT - y;
        }

        esp_err_t err =
            esp_lcd_panel_draw_bitmap(
                g_panel,
                0,
                y,
                TFT_WIDTH,
                y + lines,
                clear_dma_buffer
            );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Error limpiando TFT y=%d: %s",
                y,
                esp_err_to_name(err)
            );

            return;
        }

        xSemaphoreTake(
            lcd_done_sem,
            portMAX_DELAY
        );
    }
}

//-----------------------------------------
esp_err_t display_draw_rgb565_blocks(
    int x,
    int y,
    int width,
    int height,
    display_block_fill_t fill,
    void *context
)
{
    if (g_panel == NULL || lcd_done_sem == NULL ||
        draw_dma_buffer[0] == NULL || draw_dma_buffer[1] == NULL ||
        fill == NULL)
        return ESP_ERR_INVALID_STATE;

    if (width <= 0 || height <= 0 || x < 0 || y < 0 ||
        width > TFT_WIDTH || height > TFT_HEIGHT ||
        x > TFT_WIDTH - width || y > TFT_HEIGHT - height)
        return ESP_ERR_INVALID_ARG;
static bool task_logged = false;

if (!task_logged)
{
    ESP_LOGI(
        TAG,
        "DISPLAY TASK: name=%s priority=%u core=%d",
        pcTaskGetName(NULL),
        (unsigned)uxTaskPriorityGet(NULL),
        xPortGetCoreID()
    );

    task_logged = true;
}
    int current_y = 0;
    int blocks = 0;
    int64_t submit_us = 0;
    int64_t wait_us = 0;
    int64_t scheduling_us = 0;
    int64_t fill_late_us = 0;
    int64_t gap_us = 0;
    int64_t max_resume_us = 0;
    int64_t previous_done_us = 0;


if (!task_logged)
{
    ESP_LOGI(
        TAG,
        "DISPLAY TASK: name=%s priority=%u core=%d",
        pcTaskGetName(NULL),
        (unsigned)uxTaskPriorityGet(NULL),
        xPortGetCoreID()
    );

    task_logged = true;
}

    /* Prime buffer 0; subsequent blocks are copied while DMA is active. */
    int first_lines = height < DISPLAY_DMA_LINES ? height : DISPLAY_DMA_LINES;
    fill(context, draw_dma_buffer[0], 0, first_lines, width);

   while (current_y < height)
{
    int lines = DISPLAY_DMA_LINES;

    if (current_y + lines > height)
        lines = height - current_y;

    int buffer_index = blocks & 1;

    int64_t t0 = esp_timer_get_time();

    esp_err_t err = esp_lcd_panel_draw_bitmap(
        g_panel,
        x,
        y + current_y,
        x + width,
        y + current_y + lines,
        draw_dma_buffer[buffer_index]
    );

    int64_t t1 = esp_timer_get_time();
    if (previous_done_us > 0)
        gap_us += t0 - previous_done_us;

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Error draw RGB565 y=%d: %s",
            current_y,
            esp_err_to_name(err)
        );
        return err;
    }

    /* Prepare the other buffer without touching the one owned by DMA.
       Keep one transfer in flight so the shared binary completion semaphore
       cannot lose notifications. The next submission follows completion. */
    int next_y = current_y + lines;
    if (next_y < height)
    {
        int next_lines = height - next_y;
        if (next_lines > DISPLAY_DMA_LINES)
            next_lines = DISPLAY_DMA_LINES;
        fill(context, draw_dma_buffer[buffer_index ^ 1], next_y,
             next_lines, width);
    }

    int64_t wait_start_us = esp_timer_get_time();
    if (xSemaphoreTake(lcd_done_sem, portMAX_DELAY) != pdTRUE)
        return ESP_FAIL;

    int64_t t2 = esp_timer_get_time();

    /* Only one transfer is in flight; after taking its semaphore this
       timestamp is stable until we submit the next block. */
    int64_t done_us = last_dma_done_us;
    int64_t delay_us = t2 - done_us;
    previous_done_us = done_us;
    /* Separate preparation continuing after completion from the time spent
       returning from the semaphore. Neither is an exact ISR wakeup latency. */
    if (wait_start_us > done_us)
        fill_late_us += wait_start_us - done_us;
    if (delay_us > max_resume_us)
        max_resume_us = delay_us;

    if (last_dma_done_us > 0 && delay_us >= 0)
        scheduling_us += delay_us;

    submit_us += t1 - t0;
    wait_us += t2 - wait_start_us;

    blocks++;
    current_y += lines;
}

    /* Average complete multi-block draws only; no logging during DMA.
       Gap ends at draw_bitmap entry, so submission cost is reported separately.
       resume and fill_late overlap gap; these values must not be added together. */
    static int sample_width, sample_height;
    static unsigned samples;
    static int64_t sum_submit, sum_wait, sum_resume, sum_late, sum_gap;
    static int64_t peak_resume;
    if (blocks > 1)
    {
        if (sample_width != width || sample_height != height)
        {
            sample_width = width;
            sample_height = height;
            samples = 0;
            sum_submit = sum_wait = sum_resume = sum_late = sum_gap = 0;
            peak_resume = 0;
        }
        samples++;
        sum_submit += submit_us;
        sum_wait += wait_us;
        sum_resume += scheduling_us;
        sum_late += fill_late_us;
        sum_gap += gap_us;
        if (max_resume_us > peak_resume)
            peak_resume = max_resume_us;
        if (samples >= 60)
        {
            double divisor = samples * 1000.0;
            ESP_LOGI(TAG,
                     "DMA TIMING: %dx%d blocks=%d n=%u "
                     "submit=%.3f ms wait=%.3f ms resume=%.3f ms "
                     "fill_late=%.3f ms gap=%.3f ms resume_max=%.3f ms",
                     width, height, blocks, samples,
                     sum_submit / divisor, sum_wait / divisor,
                     sum_resume / divisor, sum_late / divisor,
                     sum_gap / divisor, peak_resume / 1000.0);
            samples = 0;
            sum_submit = sum_wait = sum_resume = sum_late = sum_gap = 0;
            peak_resume = 0;
        }
    }

    return ESP_OK;
}
//-----------------------------------
static void copy_rgb565_block(void *context, uint16_t *destination,
                              int first_line, int lines, int width)
{
    const uint16_t *pixels = context;
    memcpy(destination, pixels + (size_t)first_line * (size_t)width,
           (size_t)width * (size_t)lines * sizeof(uint16_t));
}

esp_err_t display_draw_rgb565(int x, int y, int width, int height,
                             const uint16_t *pixels)
{
    if (pixels == NULL)
        return ESP_ERR_INVALID_STATE;
    return display_draw_rgb565_blocks(x, y, width, height,
                                      copy_rgb565_block, (void *)pixels);
}

void display_clear(void)
{
    if (g_panel == NULL ||
        clear_dma_buffer == NULL)
    {
        return;
    }

    limpiar_tft();
}

//-----------------------------------
esp_err_t display_init(void)
{
    g_panel = iniciar_tft();

    if (g_panel == NULL)
    {
        return ESP_FAIL;
    }

    for (int i = 0; i < 2; i++)
    {
    draw_dma_buffer[i] =
    heap_caps_malloc(
        TFT_WIDTH *
        DISPLAY_DMA_LINES *
        sizeof(uint16_t),
        MALLOC_CAP_DMA |
        MALLOC_CAP_INTERNAL
    );

if (draw_dma_buffer[i] == NULL)
{
    ESP_LOGE(
        TAG,
        "No pude reservar draw DMA buffer %d", i
    );

    for (int j = 0; j < i; j++)
    {
        heap_caps_free(draw_dma_buffer[j]);
        draw_dma_buffer[j] = NULL;
    }
    return ESP_ERR_NO_MEM;
}
    }
    scale_dma_buffer =
    heap_caps_malloc(
        240 *
        24 *
        sizeof(uint16_t),
        MALLOC_CAP_DMA |
        MALLOC_CAP_INTERNAL
    );

if (scale_dma_buffer == NULL)
{
    ESP_LOGE(
        TAG,
        "No pude reservar scale DMA buffer"
    );

    return ESP_ERR_NO_MEM;
}

    return ESP_OK;
}

//-------------------------------------
esp_err_t display_draw_rgb565_scaled_3_2(
    int x,
    int y,
    const uint16_t *src,
    int src_width,
    int src_height
)
{
    if (g_panel == NULL ||
        lcd_done_sem == NULL ||
        src == NULL ||
        src_width <= 0 ||
        src_height <= 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Escala:
     *
     * 2 pixels fuente -> 3 destino
     * 2 lineas fuente -> 3 destino
     *
     * Procesamos 16 lineas fuente por bloque:
     *
     * 16 -> 24 lineas destino
     */

    const int src_lines_per_block = 16;
    const int dst_lines_per_block = 24;

    const int dst_width =
        (src_width * 3) / 2;

    if (scale_dma_buffer == NULL)
{
    return ESP_ERR_INVALID_STATE;
}

    uint16_t *dma_buffer =
    scale_dma_buffer;

    int src_y = 0;
    int dst_y = 0;

    while (src_y < src_height)
    {
        int src_lines =
            src_lines_per_block;

        if (src_y + src_lines > src_height)
        {
            src_lines =
                src_height - src_y;
        }

        /*
         * Siempre trabajamos en pares.
         */
        if (src_lines & 1)
        {
            src_lines--;
        }

        int output_lines =
            (src_lines * 3) / 2;

        int local_dst_y = 0;

        for (int sy = 0;
             sy < src_lines;
             sy += 2)
        {
            const uint16_t *line0 =
                &src[
                    (src_y + sy) *
                    src_width
                ];

            const uint16_t *line1 =
                &src[
                    (src_y + sy + 1) *
                    src_width
                ];

            uint16_t *dst0 =
                &dma_buffer[
                    local_dst_y *
                    dst_width
                ];

            uint16_t *dst1 =
                &dma_buffer[
                    (local_dst_y + 1) *
                    dst_width
                ];

            uint16_t *dst2 =
                &dma_buffer[
                    (local_dst_y + 2) *
                    dst_width
                ];

            int dx = 0;

            for (int sx = 0;
                 sx < src_width;
                 sx += 2)
            {
                uint16_t a =
                    line0[sx];

                uint16_t b =
                    line0[sx + 1];

                uint16_t c =
                    line1[sx];

                uint16_t d =
                    line1[sx + 1];

                /*
                 * Horizontal 2 -> 3
                 */

                dst0[dx]     = a;
                dst0[dx + 1] = a;
                dst0[dx + 2] = b;

                dst1[dx]     = a;
                dst1[dx + 1] = a;
                dst1[dx + 2] = b;

                dst2[dx]     = c;
                dst2[dx + 1] = c;
                dst2[dx + 2] = d;

                dx += 3;
            }

            local_dst_y += 3;
        }

        esp_err_t err =
            esp_lcd_panel_draw_bitmap(
                g_panel,
                x,
                y + dst_y,
                x + dst_width,
                y + dst_y + output_lines,
                dma_buffer
            );

        if (err != ESP_OK)
{
    ESP_LOGE(
        TAG,
        "Error scaler y=%d: %s",
        dst_y,
        esp_err_to_name(err)
    );

    return err;
}

        xSemaphoreTake(
            lcd_done_sem,
            portMAX_DELAY
        );

        src_y += src_lines;
        dst_y += output_lines;
    }

   

    return ESP_OK;
}

//-------------------------------------

void display_fill_rect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
)
{
    if (g_panel == NULL ||
        width <= 0 ||
        height <= 0)
    {
        return;
    }

    const int stripe_lines = 8;

    uint16_t *buffer =
        heap_caps_malloc(
            width *
            stripe_lines *
            sizeof(uint16_t),
            MALLOC_CAP_DMA |
            MALLOC_CAP_INTERNAL
        );

    if (buffer == NULL)
    {
        ESP_LOGE(
            TAG,
            "Sin memoria para fill_rect"
        );

        return;
    }

    for (int i = 0;
         i < width * stripe_lines;
         i++)
    {
        buffer[i] = color;
    }

    int remaining = height;
    int current_y = y;

    while (remaining > 0)
{
    int lines =
        remaining > stripe_lines
            ? stripe_lines
            : remaining;

    esp_err_t err =
        display_draw_rgb565(
            x,
            current_y,
            width,
            lines,
            buffer
        );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Error fill_rect y=%d: %s",
            current_y,
            esp_err_to_name(err)
        );

        break;
    }

    current_y += lines;
    remaining -= lines;
}

    heap_caps_free(buffer);
}
//-------------------------

typedef struct
{
    char c;
    uint8_t rows[7];
} glyph5x7_t;

static const glyph5x7_t font5x7[] =
{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },

    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },

    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E } },
    { '6', { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },

    { '>', { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } }
};
static const uint8_t *font5x7_find(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        c -= ('a' - 'A');
    }

    for (size_t i = 0;
         i < sizeof(font5x7) / sizeof(font5x7[0]);
         i++)
    {
        if (font5x7[i].c == c)
        {
            return font5x7[i].rows;
        }
    }

    return font5x7[0].rows;
}
//---------------------------

void display_draw_text(
    int x,
    int y,
    const char *text,
    uint16_t foreground,
    uint16_t background
)
{
    if (text == NULL)
    {
        return;
    }

    const int glyph_width = 5;
    const int glyph_height = 7;

    /*
     * Dejamos un pixel de separación horizontal.
     */
    const int char_width = 6;

    int cursor_x = x;

    while (*text != '\0')
    {
        const uint8_t *glyph =
            font5x7_find(*text);

        uint16_t buffer[
            glyph_width *
            glyph_height
        ];

        for (int row = 0;
             row < glyph_height;
             row++)
        {
            for (int col = 0;
                 col < glyph_width;
                 col++)
            {
                bool pixel_on =
                    (glyph[row] &
                     (1 << (4 - col))) != 0;

                buffer[
                    row * glyph_width +
                    col
                ] =
                    pixel_on
                        ? foreground
                        : background;
            }
        }

        esp_err_t err =
            display_draw_rgb565(
                cursor_x,
                y,
                glyph_width,
                glyph_height,
                buffer
            );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Error dibujando caracter '%c': %s",
                *text,
                esp_err_to_name(err)
            );

            return;
        }

        /*
         * Limpiar la columna de separación.
         */
        display_fill_rect(
            cursor_x + glyph_width,
            y,
            1,
            glyph_height,
            background
        );

        cursor_x += char_width;
        text++;
    }
}
