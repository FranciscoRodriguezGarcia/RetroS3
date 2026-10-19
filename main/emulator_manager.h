#pragma once

#include <stdbool.h>

#include "rom_manager.h"

bool emulator_manager_start(
    const rom_entry_t *rom
);

void emulator_manager_stop(void);

bool emulator_manager_is_running(void);