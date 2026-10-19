#pragma once

#include <stdbool.h>

typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_ON,
    STATUS_LED_HEARTBEAT,
    STATUS_LED_BLINK_FAST
} status_led_mode_t;

void status_led_init(void);
void status_led_set_mode(status_led_mode_t mode);