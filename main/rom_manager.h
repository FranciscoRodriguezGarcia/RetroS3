#pragma once

#include <stddef.h>
#include <stdbool.h>

#define ROM_NAME_MAX 128
#define ROM_PATH_MAX 256
#define ROM_MAX_COUNT 64

typedef enum
{
    SYSTEM_UNKNOWN = 0,
    SYSTEM_GAMEBOY,
    SYSTEM_GAMEBOY_COLOR,
    SYSTEM_NES,
    SYSTEM_MEGADRIVE
} emulator_system_t;

typedef struct
{
    char name[ROM_NAME_MAX];
    char path[ROM_PATH_MAX];

    emulator_system_t system;

    size_t size;
} rom_entry_t;

typedef struct
{
    rom_entry_t entries[ROM_MAX_COUNT];
    size_t count;
} rom_list_t;

bool rom_manager_scan(
    rom_list_t *list
);

const char *rom_manager_system_name(
    emulator_system_t system
);


typedef struct
{
    emulator_system_t system;
    char display_name[32];
    size_t rom_count;
} system_entry_t;

typedef struct
{
    system_entry_t entries[8];
    size_t count;
} system_list_t;

bool rom_manager_get_systems(
    const rom_list_t *roms,
    system_list_t *systems
);
