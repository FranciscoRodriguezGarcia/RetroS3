#include "megaDrive.h"
#include "emulator_diag.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "gwenesis_bus.h"
#include "m68k.h"
#include "z80inst.h"
#include "gwenesis_vdp.h"
#include "ym2612.h"
#include "gwenesis_sn76489.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config.h"
#include "esp_log.h"
#include "display.h"
#include "input.h"
#include "gwenesis_io.h"

#define MD_TARGET_FPS 60

#define MD_FRAME_TIME_US \
    (1000000 / MD_TARGET_FPS)


#define MD_VIDEO_WIDTH   240
#define MD_VIDEO_HEIGHT  168
#define MD_SOURCE_WIDTH  320
#define MD_SOURCE_HEIGHT 240
#define MD_SOURCE_BYTES  (MD_SOURCE_WIDTH * MD_SOURCE_HEIGHT * 4)

#define MD_VIDEO_PIXELS \
    (MD_VIDEO_WIDTH * MD_VIDEO_HEIGHT)

#define MD_VIDEO_BYTES \
    (MD_VIDEO_PIXELS * sizeof(uint16_t))


#define MD_OUTPUT_WIDTH   240
#define MD_OUTPUT_HEIGHT  168

#define MD_X_OFFSET \
    ((TFT_WIDTH - MD_OUTPUT_WIDTH) / 2)

#define MD_Y_OFFSET \
    ((TFT_HEIGHT - MD_OUTPUT_HEIGHT) / 2)
// --------------------------------------------------
// Audio buffers requeridos por Gwenesis
// --------------------------------------------------

#define AUDIO_BUFFER_LENGTH 2048

#define FRAMEBUFFER_COUNT 2

static uint16_t *framebuffers[FRAMEBUFFER_COUNT];
extern int screen_height;
static uint8_t *source_buffers[FRAMEBUFFER_COUNT];
static uint16_t *dma_buffers[2];

static QueueHandle_t free_frames;
static QueueHandle_t ready_frames;


int16_t gwenesis_ym2612_buffer[AUDIO_BUFFER_LENGTH];
int16_t gwenesis_sn76489_buffer[AUDIO_BUFFER_LENGTH];

int ym2612_index = 0;
int ym2612_clock = 0;



int system_clock = 0;
int scan_line = 0;

static const char *TAG = "RETROS3";


// --------------------------------------------------
// Input requerido por Gwenesis
// --------------------------------------------------

extern unsigned char button_state[3];

void gwenesis_io_get_buttons(void)
{
    input_state_t input =
        input_get_state();

    /*
     * Gwenesis usa active-low:
     *
     * 1 = botón suelto
     * 0 = botón presionado
     */
    button_state[0] = 0xFF;

    // UP
    if (input_is_pressed(
            input,
            INPUT_BUTTON_UP))
    {
        button_state[0] &=
            ~(1 << 0);
    }

    // DOWN
    if (input_is_pressed(
            input,
            INPUT_BUTTON_DOWN))
    {
        button_state[0] &=
            ~(1 << 1);
    }

    // LEFT
    if (input_is_pressed(
            input,
            INPUT_BUTTON_LEFT))
    {
        button_state[0] &=
            ~(1 << 2);
    }

    // RIGHT
    if (input_is_pressed(
            input,
            INPUT_BUTTON_RIGHT))
    {
        button_state[0] &=
            ~(1 << 3);
    }

    /*
     * Mega Drive:
     *
     * bit 4 = C
     * bit 5 = B
     * bit 6 = A
     * bit 7 = START
     */

    if (input_is_pressed(
            input,
            INPUT_BUTTON_A))
    {
        button_state[0] &=
            ~(1 << 6);
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_B))
    {
        button_state[0] &=
            ~(1 << 5);
    }

    /*
     * Usamos X físico como C de Mega Drive.
     */
    if (input_is_pressed(
            input,
            INPUT_BUTTON_X))
    {
        button_state[0] &=
            ~(1 << 4);
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_START))
    {
        button_state[0] &=
            ~(1 << 7);
    }
}

// ------------------------------------------------------------

static void ejecutar_frame(
    uint8_t *framebuffer,
    bool render_video
)
{
    extern unsigned char gwenesis_vdp_regs[0x20];
    extern unsigned int gwenesis_vdp_status;
    extern int hint_pending;
    extern int screen_width;
    extern int screen_height;
    extern int zclk;

    int lines_per_frame =
        REG1_PAL
        ? LINES_PER_FRAME_PAL
        : LINES_PER_FRAME_NTSC;

    int hint_counter =
        gwenesis_vdp_regs[10];

    screen_width =
        REG12_MODE_H40
        ? 320
        : 256;

    screen_height =
        REG1_PAL
        ? 240
        : 224;

    if (render_video)
    {
        gwenesis_vdp_set_buffers(framebuffer, NULL);
    }

    gwenesis_vdp_render_config();

    system_clock = 0;
    zclk = 0;

    ym2612_clock = 0;
    ym2612_index = 0;

    scan_line = 0;

    while (scan_line < lines_per_frame)
    {
        int target =
            system_clock +
            VDP_CYCLES_PER_LINE;

        // CPU principal
        m68k_run(target);

        // Z80/audio deshabilitados
        // z80_run(target);
        // gwenesis_SN76489_run(target);
        // ym2612_run(target);

        /*
         * Render vertical reducido.
         *
         * NTSC:
         * 224 lineas fuente -> 168 lineas destino.
         *
         * De cada grupo de 4:
         * 0,1,2 se renderizan
         * 3 se descarta.
         */
        if (render_video &&
            scan_line < screen_height)
        {
            gwenesis_vdp_render_line(scan_line);
        }

        /*
         * H interrupt
         */
        if (--hint_counter < 0)
        {
            if (REG0_LINE_INTERRUPT &&
                scan_line <= screen_height)
            {
                hint_pending = 1;

                if ((gwenesis_vdp_status &
                     STATUS_VIRQPENDING) == 0)
                {
                    m68k_update_irq(4);
                }
            }

            hint_counter =
                REG10_LINE_COUNTER;
        }

        scan_line++;

        /*
         * Inicio de VBlank
         */
        if (scan_line == screen_height)
        {
            if (REG1_VBLANK_INTERRUPT)
            {
                gwenesis_vdp_status |=
                    STATUS_VIRQPENDING;

                m68k_set_irq(6);
            }

            /*
             * Como Z80 esta deshabilitado,
             * estas IRQ ya no son necesarias.
             */
            // z80_irq_line(1);
        }

        /*
         * Tampoco necesitamos limpiar
         * la IRQ del Z80.
         */
        // if (scan_line == screen_height + 1)
        // {
        //     z80_irq_line(0);
        // }

        system_clock +=
            VDP_CYCLES_PER_LINE;
    }

    /*
     * Gwenesis cuenta ciclos acumulativamente.
     * Los volvemos relativos al próximo frame.
     */
    m68k.cycles -= system_clock;
}

static void preparar_stripe_dma(
    const uint16_t *src,
    uint16_t *dst,
    int start_y,
    int lines
)
{
    for (int y = 0; y < lines; y++)
    {
        const uint16_t *src_line =
            &src[
                (start_y + y) *
                MD_VIDEO_WIDTH
            ];

        uint16_t *dst_line =
            &dst[
                y *
                MD_OUTPUT_WIDTH
            ];

        for (int x = 0;
             x < MD_OUTPUT_WIDTH;
             x++)
        {
            dst_line[x] =
                __builtin_bswap16(
                    src_line[x]
                );
        }
    }
}

// Gwenesis' host renderer writes 4 bytes per pixel (RGB + padding).
// Convert to the existing 240x168 RGB565 display pipeline.
static void convertir_frame(const uint8_t *src, uint16_t *dst)
{
    int height = screen_height == 240 ? 240 : 224;
    int top = (MD_SOURCE_HEIGHT - height) / 2;
    for (int y = 0; y < MD_VIDEO_HEIGHT; ++y) {
        int sy = top + (y * height) / MD_VIDEO_HEIGHT;
        for (int x = 0; x < MD_VIDEO_WIDTH; ++x) {
            int sx = (x * MD_SOURCE_WIDTH) / MD_VIDEO_WIDTH;
            const uint8_t *pixel = src + ((sy * MD_SOURCE_WIDTH + sx) * 4);
            dst[y * MD_VIDEO_WIDTH + x] =
                ((uint16_t)(pixel[0] & 0xf8) << 8) |
                ((uint16_t)(pixel[1] & 0xfc) << 3) |
                (pixel[2] >> 3);
        }
    }
}



// ------------------------------------------------------------

static void display_task(void *arg)
{
    int buffer_index = 0;

    uint32_t frame = 0;
    uint32_t frames_in_window = 0;

    int64_t last_report_us =
        esp_timer_get_time();

    while (true)
    {
        int frame_index;

        xQueueReceive(
            ready_frames,
            &frame_index,
            portMAX_DELAY
        );

        uint16_t *framebuffer =
            framebuffers[frame_index];

       for (int y = 0;
     y < MD_OUTPUT_HEIGHT;
     y += TFT_STRIPE_LINES)
{
    int lines = TFT_STRIPE_LINES;

    if (y + lines > MD_OUTPUT_HEIGHT)
    {
        lines =
            MD_OUTPUT_HEIGHT - y;
    }

    uint16_t *dma_buffer =
        dma_buffers[buffer_index];

    preparar_stripe_dma(
        framebuffer,
        dma_buffer,
        y,
        lines
    );

 esp_err_t err =
    display_draw_rgb565(
        MD_X_OFFSET,
        MD_Y_OFFSET + y,
        MD_OUTPUT_WIDTH,
        lines,
        dma_buffer
    );

if (err != ESP_OK)
{
    ESP_LOGE(
        TAG,
        "Error enviando frame al display: %s",
        esp_err_to_name(err)
    );
}

    buffer_index ^= 1;
}

        // Llegamos acá cuando las 240 líneas
        // del frame ya fueron transmitidas.
        frame++;
        frames_in_window++;

        int64_t now_us =
            esp_timer_get_time();

        int64_t elapsed_us =
            now_us - last_report_us;

        if (elapsed_us >= 1000000)
        {
            double elapsed_seconds =
                elapsed_us / 1000000.0;

            double fps =
                frames_in_window /
                elapsed_seconds;

            ESP_LOGI(
                TAG,
                "DISPLAY FPS: %.1f | total frames: %lu",
                fps,
                (unsigned long)frame
            );

            frames_in_window = 0;
            last_report_us = now_us;
        }

        // Este framebuffer vuelve a quedar disponible
        // para Gwenesis.
        xQueueSend(
            free_frames,
            &frame_index,
            portMAX_DELAY
        );
    }
}
// -------------------------------------------------------
static void emulation_task(void *arg)
{
    uint32_t frame = 0;
    uint32_t rendered_frames = 0;
    uint32_t frames_in_window = 0;

    int64_t last_report_us =
        esp_timer_get_time();

    int64_t next_frame_us =
        last_report_us;

    while (true)
    {
        bool render_video =
            (frame % 2) == 0;

        if (render_video)
        {
            int frame_index;

            xQueueReceive(
                free_frames,
                &frame_index,
                portMAX_DELAY
            );

            ejecutar_frame(
                source_buffers[frame_index],
                true
            );

            convertir_frame(source_buffers[frame_index], framebuffers[frame_index]);

            xQueueSend(
                ready_frames,
                &frame_index,
                portMAX_DELAY
            );

            rendered_frames++;
        }
        else
        {
            ejecutar_frame(
                NULL,
                false
            );
        }

        frame++;
        frames_in_window++;

        /*
         * Pacing a 60 Hz.
         *
         * Si terminamos antes del momento en que debería
         * comenzar el próximo frame, esperamos.
         *
         * Si la emulación ya viene atrasada, no esperamos.
         */
        next_frame_us += MD_FRAME_TIME_US;

        int64_t now_us =
            esp_timer_get_time();

        int64_t remaining_us =
            next_frame_us - now_us;

        if (remaining_us > 0)
        {
            /*
             * Dormimos la mayor parte del tiempo usando
             * FreeRTOS para permitir que IDLE0 ejecute.
             */
            if (remaining_us >= 2000)
            {
                TickType_t ticks =
                    pdMS_TO_TICKS(
                        remaining_us / 1000
                    );

                if (ticks > 0)
                {
                    vTaskDelay(ticks);
                }
            }

            /*
             * Terminamos de ajustar el frame con espera
             * corta. El margen suele ser pequeño.
             */
            while (esp_timer_get_time() < next_frame_us)
            {
                taskYIELD();
            }
        }
        else
        {
            /*
             * Si quedamos muy atrasados, resincronizamos
             * para no acumular deuda indefinidamente.
             */
            if (remaining_us < -100000)
            {
                next_frame_us =
                    esp_timer_get_time();
            }
        }

        int64_t report_now_us =
            esp_timer_get_time();

        int64_t elapsed_us =
            report_now_us -
            last_report_us;

        if (elapsed_us >= 1000000)
        {
            double elapsed_seconds =
                elapsed_us / 1000000.0;

            double fps =
                frames_in_window /
                elapsed_seconds;

            ESP_LOGI(
                TAG,
                "EMU FPS: %.1f | emulados: %lu | renderizados: %lu",
                fps,
                (unsigned long)frame,
                (unsigned long)rendered_frames
            );

            frames_in_window = 0;
            last_report_us = report_now_us;
        }
    }
}
// -------------------------------------------------------



static unsigned char *cargar_rom(
    const char *path,
    size_t *rom_size
)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        emulator_diag_set(EMU_DIAG_SD_OPEN, "No se pudo abrir archivo MD");
        ESP_LOGE(TAG, "No pude abrir ROM: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);

    long size = ftell(f);

    if (size <= 0 || size > MAX_ROM_SIZE) {
        emulator_diag_set(EMU_DIAG_ROM_INVALID, "Tamano MD invalido");
        ESP_LOGE(TAG, "Tamanio ROM invalido");
        fclose(f);
        return NULL;
    }

    rewind(f);

    ESP_LOGI(
        TAG,
        "ROM encontrada: %ld bytes",
        size
    );

    unsigned char *buffer =
        heap_caps_malloc(
            size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

    if (buffer == NULL) {
        emulator_diag_set(EMU_DIAG_MEMORY, "PSRAM para archivo MD");
        ESP_LOGE(TAG, "No pude reservar PSRAM");
        fclose(f);
        return NULL;
    }

    size_t leidos =
        fread(buffer, 1, size, f);

    fclose(f);

    if (leidos != size) {
        emulator_diag_set(EMU_DIAG_SD_READ, "Lectura incompleta MD");
        ESP_LOGE(
            TAG,
            "Lectura incompleta: %u/%ld",
            (unsigned)leidos,
            size
        );

        heap_caps_free(buffer);
        return NULL;
    }

    *rom_size = size;

    ESP_LOGI(
        TAG,
        "ROM copiada a PSRAM en %p",
        buffer
    );

    return buffer;
}

//----------------------------------------------------
bool megadrive_start(const char *rom_path)
{
    size_t rom_size = 0;

    unsigned char *rom =
        cargar_rom(
            rom_path,
            &rom_size
        );

    if (rom == NULL)
    {
        return false;
    }

    ESP_LOGI(
        TAG,
        "Cargando cartucho en Gwenesis..."
    );

    load_cartridge(
        rom,
        rom_size
    );

    ESP_LOGI(TAG, "Cartucho OK");

    power_on();

    ESP_LOGI(TAG, "Power ON OK");

    reset_emulation();

    ESP_LOGI(TAG, "Reset OK");

    /*
     * Reservar framebuffers MD
     */
    for (int i = 0;
         i < FRAMEBUFFER_COUNT;
         i++)
    {
        framebuffers[i] =
            heap_caps_malloc(
                MD_VIDEO_BYTES,
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );

        if (framebuffers[i] == NULL)
        {
            emulator_diag_set(EMU_DIAG_MEMORY, "Framebuffer MD sin PSRAM");
            ESP_LOGE(
                TAG,
                "No pude reservar framebuffer %d",
                i
            );

            return false;
        }

        memset(
            framebuffers[i],
            0,
            MD_VIDEO_BYTES
        );

        source_buffers[i] = heap_caps_calloc(1, MD_SOURCE_BYTES,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (source_buffers[i] == NULL) {
            emulator_diag_set(EMU_DIAG_MEMORY, "Buffer RGB MD sin PSRAM");
            ESP_LOGE(TAG, "No pude reservar buffer RGB Gwenesis %d", i);
            return false;
        }
    }

    /*
     * Buffers DMA del pipeline Mega Drive
     */
    for (int i = 0; i < 2; i++)
    {
        dma_buffers[i] =
            heap_caps_malloc(
                TFT_STRIPE_PIXELS *
                    sizeof(uint16_t),
                MALLOC_CAP_DMA |
                MALLOC_CAP_INTERNAL
            );

        if (dma_buffers[i] == NULL)
        {
            emulator_diag_set(EMU_DIAG_MEMORY, "DMA interno MD insuficiente");
            ESP_LOGE(
                TAG,
                "No pude reservar DMA buffer %d",
                i
            );

            return false;
        }
    }

    free_frames =
        xQueueCreate(
            FRAMEBUFFER_COUNT,
            sizeof(int)
        );

    ready_frames =
        xQueueCreate(
            FRAMEBUFFER_COUNT,
            sizeof(int)
        );

    if (free_frames == NULL ||
        ready_frames == NULL)
    {
        emulator_diag_set(EMU_DIAG_MEMORY, "Colas FreeRTOS MD");
        ESP_LOGE(
            TAG,
            "Error creando queues"
        );

        return false;
    }

    for (int i = 0;
         i < FRAMEBUFFER_COUNT;
         i++)
    {
        xQueueSend(
            free_frames,
            &i,
            portMAX_DELAY
        );
    }

    xTaskCreatePinnedToCore(
        emulation_task,
        "gwenesis",
        8192,
        NULL,
        5,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        display_task,
        "display_md",
        4096,
        NULL,
        4,
        NULL,
        1
    );

    ESP_LOGI(
        TAG,
        "Mega Drive iniciado: EMU core 0 + DISPLAY core 1"
    );

    return true;
}
