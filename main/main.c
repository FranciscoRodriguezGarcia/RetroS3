#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "storage.h"
#include "display.h"
#include "rom_manager.h"
#include "emulator_manager.h"
#include "launcher.h"
#include "input.h"
#include "status_led.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *TAG = "FRANTASTIC";

static rom_list_t roms;

void app_main(void)
{
    status_led_init();

    // Mientras inicializa Retro53
    status_led_set_mode(STATUS_LED_BLINK_FAST);



    printf("\n");
    printf("=== FRANTASTIC ===\n");

    ESP_LOGI(
        TAG,
        "PSRAM libre: %u bytes",
        (unsigned)heap_caps_get_free_size(
            MALLOC_CAP_SPIRAM
        )
    );

    // -----------------------------
    // Storage
    // -----------------------------

    if (storage_mount() != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "No se pudo montar SD"
        );
        if (display_init()==ESP_OK) {
            display_clear();
            display_draw_text(130,96,"FRANTASTIC",0xffff,0);
            display_draw_text(100,120,"SD NO DISPONIBLE",0xffff,0);
        }
        return;
    }

ESP_LOGI(TAG, "Verificando contenido de /sdcard");

DIR *dir = opendir("/sdcard");

if (dir == NULL)
{
    ESP_LOGE(
        TAG,
        "No pude abrir /sdcard: errno=%d (%s)",
        errno,
        strerror(errno)
    );
}
else
{
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        ESP_LOGI(
            TAG,
            "SD ROOT: %s",
            entry->d_name
        );
    }

    closedir(dir);
}


    // -----------------------------
    // Display
    // -----------------------------

    if (display_init() != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "No se pudo inicializar display"
        );
        return;
    }

    display_clear();


    //-------------------------------
    // Input 
    //-------------------------------
    input_init();


    // -----------------------------
    // ROM Manager
    // -----------------------------

    if (!rom_manager_scan(&roms))
    {
        ESP_LOGE(
            TAG,
            "Error escaneando ROMs"
        );
        return;
    }

    for (size_t i = 0;
         i < roms.count;
         i++)
    {
        ESP_LOGI(
            TAG,
            "[%u] %s | %s | %u bytes",
            (unsigned)i,
            roms.entries[i].name,
            rom_manager_system_name(
                roms.entries[i].system
            ),
            (unsigned)roms.entries[i].size
        );
    }

    /*
     * Temporal:
     * arrancamos la primera ROM encontrada.
     *
     * Después esto lo reemplaza el launcher.
     */
    if (roms.count == 0)
    {
        ESP_LOGW(
            TAG,
            "No hay ROMs disponibles"
        );
    }
// ------------------------------------------------
// Launcher
// ------------------------------------------------

if (!launcher_start(&roms))
{
    ESP_LOGE(
        TAG,
        "No se pudo iniciar launcher"
    );

    return;
}

ESP_LOGI(
    TAG,
    "Launcher listo"
);
}
