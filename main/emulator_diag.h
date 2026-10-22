#pragma once

#include <stddef.h>

typedef enum {
    EMU_DIAG_NONE = 0,
    EMU_DIAG_SD_OPEN,
    EMU_DIAG_SD_READ,
    EMU_DIAG_ROM_INVALID,
    EMU_DIAG_MEMORY,
    EMU_DIAG_CORE,
    EMU_DIAG_TASK,
    EMU_DIAG_SAVE,
    EMU_DIAG_UNKNOWN
} emulator_diag_code_t;

void emulator_diag_clear(void);
void emulator_diag_set(emulator_diag_code_t code, const char *detail);
emulator_diag_code_t emulator_diag_code(void);
const char *emulator_diag_detail(void);
const char *emulator_diag_label(void);
