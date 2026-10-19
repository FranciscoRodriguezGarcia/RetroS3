#pragma once

#include <stdbool.h>

bool megadrive_start(const char *rom_path);
void megadrive_stop(void);
bool megadrive_is_running(void);