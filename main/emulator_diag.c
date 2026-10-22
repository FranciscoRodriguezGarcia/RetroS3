#include "emulator_diag.h"

#include <stdio.h>

static emulator_diag_code_t code = EMU_DIAG_NONE;
static char detail[64];

void emulator_diag_clear(void)
{
    code = EMU_DIAG_NONE;
    detail[0] = '\0';
}

void emulator_diag_set(emulator_diag_code_t new_code, const char *message)
{
    code = new_code;
    snprintf(detail, sizeof(detail), "%s", message ? message : "");
}

emulator_diag_code_t emulator_diag_code(void) { return code; }
const char *emulator_diag_detail(void) { return detail; }

const char *emulator_diag_label(void)
{
    switch (code) {
    case EMU_DIAG_SD_OPEN:     return "SD-01: NO SE PUEDE ABRIR";
    case EMU_DIAG_SD_READ:     return "SD-02: LECTURA INCOMPLETA";
    case EMU_DIAG_ROM_INVALID: return "ROM-03: ARCHIVO INVALIDO";
    case EMU_DIAG_MEMORY:     return "MEM-04: SIN MEMORIA";
    case EMU_DIAG_CORE:       return "CORE-05: ERROR DEL CORE";
    case EMU_DIAG_TASK:       return "TASK-06: ERROR DE TAREA";
    case EMU_DIAG_SAVE:       return "SAVE-07: ERROR DE PARTIDA";
    default:                  return "EMU-99: ERROR DESCONOCIDO";
    }
}
