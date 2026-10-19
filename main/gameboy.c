#include "gameboy.h"
#include "retro_audio.h"

/* Fixed mono buffer; gnuboy_set_soundbuffer and callback use sample counts. */
static int16_t gb_pcm[512];
static void gameboy_audio_callback(void *buffer, size_t samples)
{
    retro_audio_submit(buffer, samples);
}


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include "esp_system.h"

#include "display.h"
#include "config.h"
#include "input.h"
#include "gnuboy.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "status_led.h"

// --------------------------------------------------
// Resolución nativa GB / GBC
// --------------------------------------------------

#define GB_FRAME_WIDTH  160
#define GB_FRAME_HEIGHT 144

#define GB_FRAME_PIXELS \
    (GB_FRAME_WIDTH * GB_FRAME_HEIGHT)

#define GB_FRAME_BYTES \
    (GB_FRAME_PIXELS * sizeof(uint16_t))


// --------------------------------------------------
// Salida escalada 3:2
//
// 160x144 -> 240x216
// --------------------------------------------------

#define GB_OUTPUT_WIDTH  240
#define GB_OUTPUT_HEIGHT 216

#define GB_X_OFFSET \
    ((TFT_WIDTH - GB_OUTPUT_WIDTH) / 2)

#define GB_Y_OFFSET \
    ((TFT_HEIGHT - GB_OUTPUT_HEIGHT) / 2)


// --------------------------------------------------
// Pipeline de video
// --------------------------------------------------

#define GB_FRAMEBUFFER_COUNT 2

static uint16_t *gb_framebuffers[
    GB_FRAMEBUFFER_COUNT
] = { NULL, NULL };

static QueueHandle_t gb_free_frames = NULL;
static QueueHandle_t gb_ready_frames = NULL;

static TaskHandle_t gb_task_handle = NULL;
static TaskHandle_t gb_display_task_handle = NULL;

/*
 * Índice del framebuffer que Gnuboy
 * está renderizando actualmente.
 */
static int gb_current_frame_index = -1;


/*
 * Mantenemos viva la ROM porque todavía
 * no confirmamos si Gnuboy conserva
 * referencias al buffer original.
 */
static unsigned char *gb_rom_data = NULL;


// --------------------------------------------------
// Log
// --------------------------------------------------

static const char *TAG = "GAMEBOY";


#define GB_SAVE_PATH_SIZE 512
#define GB_AUTOSAVE_US (30LL * 1000000)
static char gb_save_path[GB_SAVE_PATH_SIZE];
static char gb_temp_path[GB_SAVE_PATH_SIZE + 4];
static char gb_backup_path[GB_SAVE_PATH_SIZE + 4];
static atomic_bool gb_save_error;

static bool gameboy_save(void)
{
    if (!gnuboy_sram_size()) return true;
    struct stat st;
    if ((mkdir(SD_MOUNT_POINT "/saves", 0777) != 0 && errno != EEXIST) ||
        (mkdir(SD_MOUNT_POINT "/saves/gb", 0777) != 0 && errno != EEXIST) ||
        (mkdir(SD_MOUNT_POINT "/saves/gbc", 0777) != 0 && errno != EEXIST)) return false;
    if (gnuboy_save_sram(gb_temp_path, false) != 0 ||
        stat(gb_temp_path, &st) != 0 || st.st_size != gnuboy_sram_size()) return false;
    // FAT needs an absent destination. Preserve a backup between renames.
    if (stat(gb_save_path, &st) == 0)
    {
        if (remove(gb_backup_path) != 0 && errno != ENOENT) return false;
        if (rename(gb_save_path, gb_backup_path) != 0) return false;
    }
    else if (errno != ENOENT) return false;
    if (rename(gb_temp_path, gb_save_path) != 0) return false;
    ESP_LOGI(TAG, "Partida guardada: %s", gb_save_path);
    return true;
}

static bool gameboy_load_save(void)
{
    if (!gnuboy_sram_size()) return true;
    struct stat st;
    if (stat(gb_save_path, &st) == 0)
    {
        if (gnuboy_load_sram(gb_save_path) == 0) return true;
        ESP_LOGE(TAG, "Guardado invalido: %s", gb_save_path);
    }
    else if (errno != ENOENT) return false;
    if (stat(gb_backup_path, &st) == 0)
    {
        if (gnuboy_load_sram(gb_backup_path) != 0) return false;
        if (remove(gb_save_path) != 0 && errno != ENOENT) return false;
        return rename(gb_backup_path, gb_save_path) == 0;
    }
    if (errno != ENOENT) return false;
    return stat(gb_save_path, &st) != 0 && errno == ENOENT;
}

// --------------------------------------------------
// Prototipos privados
// --------------------------------------------------

static unsigned char *gameboy_load_rom(
    const char *path,
    size_t *rom_size
);

static void gameboy_free_framebuffers(void);

static void gameboy_video_callback(
    void *buffer
);

static int gameboy_get_pad_state(void);

static void gameboy_task(
    void *arg
);

static void gameboy_display_task(
    void *arg
);


// --------------------------------------------------
// Liberar framebuffers
// --------------------------------------------------

static void gameboy_free_framebuffers(void)
{
    for (int i = 0;
         i < GB_FRAMEBUFFER_COUNT;
         i++)
    {
        if (gb_framebuffers[i] != NULL)
        {
            heap_caps_free(
                gb_framebuffers[i]
            );

            gb_framebuffers[i] = NULL;
        }
    }
}


// --------------------------------------------------
// Cargar ROM desde SD a PSRAM
// --------------------------------------------------

static unsigned char *gameboy_load_rom(
    const char *path,
    size_t *rom_size
)
{
    FILE *f =
        fopen(path, "rb");

    if (f == NULL)
    {
        ESP_LOGE(
            TAG,
            "No pude abrir ROM: %s",
            path
        );

        return NULL;
    }

    fseek(
        f,
        0,
        SEEK_END
    );

    long size =
        ftell(f);

    if (size <= 0)
    {
        ESP_LOGE(
            TAG,
            "Tamanio ROM invalido"
        );

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
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (buffer == NULL)
    {
        ESP_LOGE(
            TAG,
            "No pude reservar PSRAM para ROM"
        );

        fclose(f);

        return NULL;
    }

    size_t read =
        fread(
            buffer,
            1,
            size,
            f
        );

    fclose(f);

    if (read != (size_t)size)
    {
        ESP_LOGE(
            TAG,
            "Lectura incompleta: %u/%ld",
            (unsigned)read,
            size
        );

        heap_caps_free(buffer);

        return NULL;
    }

    *rom_size =
        (size_t)size;

    return buffer;
}


// --------------------------------------------------
// Callback de video de Gnuboy
//
// NO dibuja.
//
// Sólo informa a la task de display que
// el framebuffer actual está terminado.
// --------------------------------------------------

static void gameboy_video_callback(
    void *buffer
)
{
    (void)buffer;

    if (gb_current_frame_index < 0 ||
        gb_ready_frames == NULL)
    {
        return;
    }

    int frame_index =
        gb_current_frame_index;

    xQueueSend(
        gb_ready_frames,
        &frame_index,
        portMAX_DELAY
    );

    gb_current_frame_index = -1;
}


// --------------------------------------------------
// Traducir input genérico Retro53 -> Gnuboy
// --------------------------------------------------

static int gameboy_get_pad_state(void)
{
    input_state_t input =
        input_get_state();

    int pad = 0;

    if (input_is_pressed(
            input,
            INPUT_BUTTON_RIGHT))
    {
        pad |= GB_PAD_RIGHT;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_LEFT))
    {
        pad |= GB_PAD_LEFT;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_UP))
    {
        pad |= GB_PAD_UP;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_DOWN))
    {
        pad |= GB_PAD_DOWN;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_A))
    {
        pad |= GB_PAD_A;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_B))
    {
        pad |= GB_PAD_B;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_SELECT))
    {
        pad |= GB_PAD_SELECT;
    }

    if (input_is_pressed(
            input,
            INPUT_BUTTON_START))
    {
        pad |= GB_PAD_START;
    }

    return pad;
}


// --------------------------------------------------
// Task Gnuboy
//
// Core 0
// --------------------------------------------------

static void gameboy_task(
    void *arg
)
{
    (void)arg;

    int64_t last_save_us = esp_timer_get_time();
    uint32_t frames = 0;
    uint32_t frames_in_window = 0;

    int64_t last_report_us =
        esp_timer_get_time();

    ESP_LOGI(
        TAG,
        "Entrando al loop de emulacion"
    );

    while (true)
    {
        bool exit_requested = input_consume_menu_request();
        int64_t save_now = esp_timer_get_time();
        // Include RTC even when SRAM banks have not changed.
        if (exit_requested || save_now - last_save_us >= GB_AUTOSAVE_US)
        {
            status_led_set_mode(STATUS_LED_ON);

            bool saved = gameboy_save();
            atomic_store(&gb_save_error, !saved);
            last_save_us = esp_timer_get_time();

            if (saved)
            {
                if (exit_requested)
                {
                    ESP_LOGI(TAG, "Guardado exitoso. Reiniciando.");
                    esp_restart();
                }
                status_led_set_mode(STATUS_LED_HEARTBEAT);
            }
            else
            {
                status_led_set_mode(STATUS_LED_BLINK_FAST);
                ESP_LOGE(TAG, "No se pudo guardar. MENU para reintentar.");
            }
        }

        int frame_index;

        /*
         * Esperar un framebuffer que no esté
         * siendo utilizado por el display.
         */
        if (xQueueReceive(
            gb_free_frames,
            &frame_index,
            pdMS_TO_TICKS(100)
        ) != pdTRUE)
        {
            /* Keep servicing MENU even if display stops returning buffers. */
            continue;
        }

        gb_current_frame_index =
            frame_index;

        /*
         * Gnuboy renderizará este frame
         * directamente en uno de nuestros
         * buffers de PSRAM.
         */
        gnuboy_set_framebuffer(
            gb_framebuffers[
                frame_index
            ]
        );

        gnuboy_set_pad(
            gameboy_get_pad_state()
        );

        /*
         * Al finalizar el frame,
         * gameboy_video_callback()
         * pondrá el índice en gb_ready_frames.
         */
        gnuboy_run(true);

        /* With LCD disabled, Gnuboy does not call the video callback. This
         * frame is still owned by emulation and must return to the free pool;
         * otherwise two such frames exhaust both PSRAM buffers permanently.
         * A published frame has already set gb_current_frame_index to -1. */
        if (gb_current_frame_index >= 0)
        {
            int unused_frame = gb_current_frame_index;
            gb_current_frame_index = -1;
            BaseType_t returned = xQueueSend(gb_free_frames, &unused_frame, 0);
            configASSERT(returned == pdTRUE);
            if (returned != pdTRUE) ESP_LOGE(TAG, "Pool GB inconsistente al devolver frame sin video");
            /* No display backpressure in this path: let audio/idle tasks run. */
            vTaskDelay(1);
        }

        frames++;
        frames_in_window++;

        int64_t now_us =
            esp_timer_get_time();

        int64_t elapsed_us =
            now_us - last_report_us;

        if (elapsed_us >= 1000000)
        {
            double seconds =
                elapsed_us /
                1000000.0;

            double fps =
                frames_in_window /
                seconds;

            ESP_LOGI(
                TAG,
                "GB EMU FPS: %.1f | frames: %lu",
                fps,
                (unsigned long)frames
            );

            frames_in_window = 0;
            last_report_us = now_us;
        }

        taskYIELD();
    }
}


// --------------------------------------------------
// Task Display GB
//
// Core 1
// --------------------------------------------------

static void gameboy_display_task(
    void *arg
)
{
    (void)arg;

    uint32_t frames = 0;
    uint32_t frames_in_window = 0;

    int64_t last_report_us =
        esp_timer_get_time();

    while (true)
    {
        int frame_index;

        /*
         * Esperar un frame terminado
         * por Gnuboy.
         */
        xQueueReceive(
            gb_ready_frames,
            &frame_index,
            portMAX_DELAY
        );

        /*
         * Escalar directamente:
         *
         * 160x144
         *    ->
         * 240x216
         *
         * El scaler prepara sus bloques DMA
         * sin framebuffer escalado intermedio.
         */
        esp_err_t err =
            display_draw_rgb565_scaled_3_2(
                GB_X_OFFSET,
                GB_Y_OFFSET,
                gb_framebuffers[
                    frame_index
                ],
                GB_FRAME_WIDTH,
                GB_FRAME_HEIGHT
            );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Error mostrando frame GB: %s",
                esp_err_to_name(err)
            );
        }

        if (atomic_load(&gb_save_error))
            display_draw_text(0, 0, "ERROR SD: MENU reintenta", 0xffff, 0xf800);
        else
            display_fill_rect(0, 0, TFT_WIDTH, 8, 0);
        frames++;
        frames_in_window++;

        int64_t now_us =
            esp_timer_get_time();

        int64_t elapsed_us =
            now_us - last_report_us;

        if (elapsed_us >= 1000000)
        {
            double seconds =
                elapsed_us /
                1000000.0;

            double fps =
                frames_in_window /
                seconds;

            ESP_LOGI(
                TAG,
                "GB DISPLAY FPS: %.1f | frames: %lu",
                fps,
                (unsigned long)frames
            );

            frames_in_window = 0;
            last_report_us = now_us;
        }

        /*
         * Ya terminamos de leer este buffer.
         *
         * Lo devolvemos a Gnuboy.
         */
        xQueueSend(
            gb_free_frames,
            &frame_index,
            portMAX_DELAY
        );
    }
}


// --------------------------------------------------
// Arranque GB / GBC
// --------------------------------------------------

static bool gameboy_start_internal(
    const char *rom_path
)
{
    if (!rom_path) return false;
    const char *name = strrchr(rom_path, '/');
    name = name ? name + 1 : rom_path;
    const char *extension = strrchr(name, '.');
    size_t stem = extension ? (size_t)(extension - name) : strlen(name);
    const char *system = extension && strcasecmp(extension, ".gbc") == 0 ? "gbc" : "gb";
    int length = snprintf(gb_save_path, sizeof(gb_save_path),
                          SD_MOUNT_POINT "/saves/%s/%.*s.sav", system, (int)stem, name);
    if (length < 0 || length >= sizeof(gb_save_path) - 4) return false;
    snprintf(gb_temp_path, sizeof(gb_temp_path), "%s.tmp", gb_save_path);
    snprintf(gb_backup_path, sizeof(gb_backup_path), "%s.bak", gb_save_path);
    atomic_store(&gb_save_error, false);
    size_t rom_size = 0;

    // --------------------------------------------------
    // ROM
    // --------------------------------------------------

    gb_rom_data =
        gameboy_load_rom(
            rom_path,
            &rom_size
        );

    if (gb_rom_data == NULL)
    {
        return false;
    }

    ESP_LOGI(
        TAG,
        "ROM cargada: %u bytes",
        (unsigned)rom_size
    );


    // --------------------------------------------------
    // Framebuffers nativos 160x144
    // --------------------------------------------------

    for (int i = 0;
         i < GB_FRAMEBUFFER_COUNT;
         i++)
    {
        gb_framebuffers[i] =
            heap_caps_malloc(
                GB_FRAME_BYTES,
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );

        if (gb_framebuffers[i] == NULL)
        {
            ESP_LOGE(
                TAG,
                "No pude reservar framebuffer GB %d",
                i
            );

            gameboy_free_framebuffers();

            heap_caps_free(
                gb_rom_data
            );

            gb_rom_data = NULL;

            return false;
        }

        memset(
            gb_framebuffers[i],
            0,
            GB_FRAME_BYTES
        );
    }


    // --------------------------------------------------
    // Inicializar Gnuboy
    // --------------------------------------------------

    int init_result =
        gnuboy_init(
            RETRO_AUDIO_RATE,
            GB_AUDIO_MONO_S16,
            GB_PIXEL_565_BE,
            gameboy_video_callback,
            gameboy_audio_callback
        );

    if (init_result != 0)
    {
        ESP_LOGE(
            TAG,
            "gnuboy_init fallo: %d",
            init_result
        );

        gameboy_free_framebuffers();

        heap_caps_free(
            gb_rom_data
        );

        gb_rom_data = NULL;

        return false;
    }


    // --------------------------------------------------
    // Cargar cartucho
    // --------------------------------------------------

    int rom_result =
        gnuboy_load_rom(
            gb_rom_data,
            rom_size
        );

    if (rom_result != 0)
    {
        ESP_LOGE(
            TAG,
            "gnuboy_load_rom fallo: %d",
            rom_result
        );

        gameboy_free_framebuffers();

        heap_caps_free(
            gb_rom_data
        );

        gb_rom_data = NULL;

        return false;
    }


    // --------------------------------------------------
    // Framebuffer inicial + reset
    // --------------------------------------------------

    gnuboy_set_framebuffer(
        gb_framebuffers[0]
    );

    gnuboy_set_soundbuffer(gb_pcm, sizeof(gb_pcm) / sizeof(gb_pcm[0]));
    gnuboy_reset(true);
    if (!gameboy_load_save())
    {
        ESP_LOGE(TAG, "No pude recuperar la partida; inicio cancelado para protegerla");
        gnuboy_free_rom();
        gameboy_free_framebuffers();
        heap_caps_free(gb_rom_data);
        gb_rom_data = NULL;
        return false;
    }

    ESP_LOGI(
        TAG,
        "Gnuboy inicializado"
    );


    // --------------------------------------------------
    // Queues de video
    // --------------------------------------------------

    gb_free_frames =
        xQueueCreate(
            GB_FRAMEBUFFER_COUNT,
            sizeof(int)
        );

    gb_ready_frames =
        xQueueCreate(
            GB_FRAMEBUFFER_COUNT,
            sizeof(int)
        );

    if (gb_free_frames == NULL ||
        gb_ready_frames == NULL)
    {
        ESP_LOGE(
            TAG,
            "No pude crear queues GB"
        );

        if (gb_free_frames != NULL)
        {
            vQueueDelete(
                gb_free_frames
            );

            gb_free_frames = NULL;
        }

        if (gb_ready_frames != NULL)
        {
            vQueueDelete(
                gb_ready_frames
            );

            gb_ready_frames = NULL;
        }

        gameboy_free_framebuffers();

        /*
         * Como Gnuboy ya cargó el cartucho,
         * por ahora conservamos gb_rom_data
         * hasta implementar shutdown completo.
         */
        
        return false;
    }


    /*
     * Inicialmente ambos framebuffers
     * están disponibles para emulación.
     */
    for (int i = 0;
         i < GB_FRAMEBUFFER_COUNT;
         i++)
    {
        xQueueSend(
            gb_free_frames,
            &i,
            portMAX_DELAY
        );
    }


    // --------------------------------------------------
    // Reset del estado del pipeline
    // --------------------------------------------------

    gb_current_frame_index = -1;


    // --------------------------------------------------
    // Display task - Core 1
    //
    // La creamos primero para que ya esté esperando
    // frames cuando arranque Gnuboy.
    // --------------------------------------------------

    BaseType_t display_result =
        xTaskCreatePinnedToCore(
            gameboy_display_task,
            "gb_display",
            4096,
            NULL,
            4,
            &gb_display_task_handle,
            1
        );

    if (display_result != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "No pude crear gameboy_display_task"
        );
          return false;
    }


    // --------------------------------------------------
    // Emulación - Core 0
    // --------------------------------------------------

    status_led_set_mode(STATUS_LED_HEARTBEAT);

    BaseType_t emu_result =
        xTaskCreatePinnedToCore(
            gameboy_task,
            "gnuboy",
            8192,
            NULL,
            5,
            &gb_task_handle,
            0
        );

    if (emu_result != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "No pude crear gameboy_task"
        );

        vTaskDelete(
            gb_display_task_handle
        );

        gb_display_task_handle = NULL;
        return false;
    }


    ESP_LOGI(
        TAG,
        "Game Boy iniciado: EMU core 0 + DISPLAY core 1"
    );
    return true;
}

/* Public entry point: all initialization failures leave the activity LED off. */
bool gameboy_start(const char *rom_path)
{
    status_led_set_mode(STATUS_LED_BLINK_FAST);

    bool started = gameboy_start_internal(rom_path);
    if (!started)
    {
        status_led_set_mode(STATUS_LED_OFF);
        return false;
    }

    return true;
}
