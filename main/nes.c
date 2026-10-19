#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "input.h"
#include "nofrendo.h"
#include "nes.h"
#include "status_led.h"

bool nes_start(const char *rom_path)
{
    if (rom_path == NULL)
    {
        status_led_set_mode(STATUS_LED_OFF);
        return false;
    }

    char *argv[] = {
        "retro53-nes",
        (char *)rom_path,
        NULL
    };

    status_led_set_mode(STATUS_LED_HEARTBEAT);
    printf("NES: iniciando %s\n", rom_path);

    int result = nofrendo_main(2, argv);

    printf("NES: finalizado, codigo=%d\n", result);
    status_led_set_mode(STATUS_LED_OFF);
    return result == 0;
}

static uint8_t nes_read_buttons(void)
{
    input_state_t state = input_get_state();
    uint8_t buttons = 0;

    if (state & INPUT_BUTTON_A)      buttons |= (1 << 0);
    if (state & INPUT_BUTTON_B)      buttons |= (1 << 1);
    if (state & INPUT_BUTTON_SELECT) buttons |= (1 << 2);
    if (state & INPUT_BUTTON_START)  buttons |= (1 << 3);
    if (state & INPUT_BUTTON_UP)     buttons |= (1 << 4);
    if (state & INPUT_BUTTON_DOWN)   buttons |= (1 << 5);
    if (state & INPUT_BUTTON_LEFT)   buttons |= (1 << 6);
    if (state & INPUT_BUTTON_RIGHT)  buttons |= (1 << 7);

    return buttons;
}
