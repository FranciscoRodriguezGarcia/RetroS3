#pragma once

#include <stdbool.h>

#include "rom_manager.h"

typedef enum
{
    LAUNCHER_SCREEN_SYSTEMS = 0,
    LAUNCHER_SCREEN_ROMS
} launcher_screen_t;

typedef struct
{
    launcher_screen_t screen;

    const rom_list_t *roms;

    system_list_t systems;

    int selected_system_index;
    int selected_rom_index;

    int scroll_offset;

} launcher_state_t;

bool launcher_start(
    const rom_list_t *roms
);