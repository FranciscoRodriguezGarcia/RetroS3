
#include "input.h"
#include "retro_audio.h"
#include <stdatomic.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define USB_FRAME_HEADER 0xA5
#define USB_COMMAND_HEADER 0xA6
#define USB_COMMAND_MENU 0x01

static atomic_bool menu_requested = false;
#define USB_TIMEOUT_MS 250

static const char *TAG = "RETRO_INPUT";

static volatile uint8_t android_buttons = 0;
static volatile int64_t last_packet_us = 0;

static input_state_t translate_android(uint8_t buttons)
{
    input_state_t state = 0;

    if (buttons & (1 << 0)) state |= INPUT_BUTTON_A;
    if (buttons & (1 << 1)) state |= INPUT_BUTTON_B;
    if (buttons & (1 << 2)) state |= INPUT_BUTTON_SELECT;
    if (buttons & (1 << 3)) state |= INPUT_BUTTON_START;

    if (buttons & (1 << 4)) state |= INPUT_BUTTON_UP;
    if (buttons & (1 << 5)) state |= INPUT_BUTTON_DOWN;
    if (buttons & (1 << 6)) state |= INPUT_BUTTON_LEFT;
    if (buttons & (1 << 7)) state |= INPUT_BUTTON_RIGHT;

    return state;
}

static void usb_input_task(void *arg)
{
    uint8_t buffer[32];
    uint8_t pending_header = 0;
    bool framed = false;
    unsigned frame_bytes = 0;
    int64_t last_rx_us = 0;

    while (true)
    {
        int received = usb_serial_jtag_read_bytes(
            buffer,
            sizeof(buffer),
            pdMS_TO_TICKS(20)
        );

        for (int i = 0; i < received; i++)
        {
            uint8_t value = buffer[i];
            if (esp_timer_get_time() - last_rx_us > 250000) {
                uint8_t type, payload;
                retro_audio_receive(0xc0, &type, &payload);
                framed = false;
                frame_bytes = 0;
                pending_header = 0;
            }
            last_rx_us = esp_timer_get_time();
            /* Each SLIP delimiter also starts the next candidate. Legacy
             * headers are accepted only at a boundary, never inside a frame. */
            if (framed && frame_bytes == 0 &&
                (value == USB_FRAME_HEADER || value == USB_COMMAND_HEADER)) {
                framed = false;
            }
            if ((pending_header == 0 && value == 0xc0) || framed) {
                uint8_t type, payload;
                if (retro_audio_receive(value, &type, &payload)) {
                    if (type == 4) {
                        android_buttons = payload;
                        last_packet_us = esp_timer_get_time();
                    } else if (type == 5 && payload == USB_COMMAND_MENU) {
                        menu_requested = true;
                    }
                }
                if (value == 0xc0) {
                    framed = true;
                    frame_bytes = 0;
                } else frame_bytes++;
                pending_header = 0;
                continue;
            }

            if (pending_header == 0)
            {
                if (value == USB_FRAME_HEADER ||
                    value == USB_COMMAND_HEADER)
                {
                    pending_header = value;
                }

                continue;
            }

            if (pending_header == USB_FRAME_HEADER)
            {
                android_buttons = value;
                last_packet_us = esp_timer_get_time();
            }
            else if (pending_header == USB_COMMAND_HEADER)
            {
                if (value == USB_COMMAND_MENU)
                {
                    menu_requested = true;
                    ESP_LOGI(TAG, "Solicitud de regreso al launcher");
                }
            }

            pending_header = 0;
        }
    }
}

void input_init(void)
{
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 2048
    };

    esp_err_t result = usb_serial_jtag_driver_install(&config);

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Error iniciando USB: %s",
                 esp_err_to_name(result));
        return;
    }
    if (!retro_audio_init()) ESP_LOGE(TAG, "No se pudo iniciar audio USB");
    menu_requested = false;
    android_buttons = 0;
    last_packet_us = 0;

    BaseType_t task_result = xTaskCreate(
        usb_input_task,
        "usb_input",
        4096,
        NULL,
        5,
        NULL
    );

    if (task_result != pdPASS)
        ESP_LOGE(TAG, "No se pudo crear usb_input_task");
    else
        ESP_LOGI(TAG, "Joystick USB inicializado");
}

input_state_t input_get_state(void)
{
    int64_t now = esp_timer_get_time();

    if (last_packet_us == 0 ||
        now - last_packet_us > USB_TIMEOUT_MS * 1000LL)
    {
        return 0;
    }

    return translate_android(android_buttons);
}

bool input_is_pressed(
    input_state_t state,
    input_button_t button
)
{
    return (state & button) != 0;
}
bool input_consume_menu_request(void)
{
    return atomic_exchange(&menu_requested, false);
}
