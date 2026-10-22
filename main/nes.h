#pragma once

#include <stdbool.h>

typedef enum {
    NES_CORE_NOFRENDO = 0,
    NES_CORE_FRANTASTIC = 1
} nes_core_t;

void nes_set_core(nes_core_t core);
bool nes_start(const char *rom_path);