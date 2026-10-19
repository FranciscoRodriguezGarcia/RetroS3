#pragma once

#include <stdbool.h>
#include <stdint.h>

// --------------------------------------------------
// Botones genéricos de Retro53
// --------------------------------------------------

typedef enum
{
    INPUT_BUTTON_UP     = 1 << 0,
    INPUT_BUTTON_DOWN   = 1 << 1,
    INPUT_BUTTON_LEFT   = 1 << 2,
    INPUT_BUTTON_RIGHT  = 1 << 3,

    INPUT_BUTTON_A      = 1 << 4,
    INPUT_BUTTON_B      = 1 << 5,
    INPUT_BUTTON_X      = 1 << 6,
    INPUT_BUTTON_Y      = 1 << 7,

    INPUT_BUTTON_START  = 1 << 8,
    INPUT_BUTTON_SELECT = 1 << 9,

    INPUT_BUTTON_L      = 1 << 10,
    INPUT_BUTTON_R      = 1 << 11

} input_button_t;


// Un uint32_t puede guardar todos esos bits.
typedef uint32_t input_state_t;


// --------------------------------------------------
// API pública
// --------------------------------------------------

void input_init(void);

input_state_t input_get_state(void);

bool input_is_pressed(
    input_state_t state,
    input_button_t button
);

bool input_consume_menu_request(void);