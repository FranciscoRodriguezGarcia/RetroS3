
#include "emulator_manager.h"
#include "gameboy.h"
#include "nes.h"
#include "megaDrive.h"
#include "emulator_diag.h"

#include "esp_log.h"
#include <stdatomic.h>

static const char *TAG = "EMU_MANAGER";

static atomic_bool running = false;

bool emulator_manager_start(const rom_entry_t *rom)
{
    emulator_diag_clear();
    if (rom == NULL)
    {
        emulator_diag_set(EMU_DIAG_ROM_INVALID, "ROM no seleccionada");
        return false;
    }

    if (running)
    {
        emulator_diag_set(EMU_DIAG_CORE, "Ya hay un emulador activo");
        ESP_LOGW(TAG, "Ya hay un emulador activo");
        return false;
    }

    bool ok = false;

    switch (rom->system)
    {
        case SYSTEM_GAMEBOY:
        case SYSTEM_GAMEBOY_COLOR:
            ESP_LOGI(TAG, "Iniciando Game Boy: %s", rom->path);
            ok = gameboy_start(rom->path);
            break;

        case SYSTEM_NES:
    ESP_LOGI(TAG, "Iniciando NES: %s", rom->path);

    running = true;
    ok = nes_start(rom->path);
    running = false;

    break;

        case SYSTEM_MEGADRIVE:
            ESP_LOGI(TAG, "Iniciando Mega Drive: %s", rom->path);
            ok = megadrive_start(rom->path);
            break;

        default:
            emulator_diag_set(EMU_DIAG_CORE, "Sistema sin emulador");
            ESP_LOGW(
                TAG,
                "Sistema no implementado: %s",
                rom_manager_system_name(rom->system)
            );
            return false;
    }

    if (!ok && emulator_diag_code() == EMU_DIAG_NONE)
    {
        emulator_diag_set(EMU_DIAG_UNKNOWN, "Consultar error del core");
    }
    if (rom->system != SYSTEM_NES)
{
    running = ok;
}

return ok;
}

void emulator_manager_stop(void)
{
    running = false;
}

bool emulator_manager_is_running(void)
{
    return running;
}
