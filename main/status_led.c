#include "status_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STATUS_LED_GPIO GPIO_NUM_6

static const char *TAG = "STATUS_LED";

static volatile status_led_mode_t current_mode = STATUS_LED_OFF;

static void status_led_task(void *arg)
{
    (void)arg;

    uint32_t ticks = 0;

    while (true)
    {
        status_led_mode_t mode = current_mode;
        int level = 0;

        switch (mode)
        {
            case STATUS_LED_ON:
                level = 1;
                break;

            case STATUS_LED_HEARTBEAT:
                // Destello de 100 ms cada segundo
                level = (ticks % 10 == 0);
                break;

            case STATUS_LED_BLINK_FAST:
                // Parpadeo de 200 ms encendido / 200 ms apagado
                level = ((ticks / 2) % 2 == 0);
                break;

            case STATUS_LED_OFF:
            default:
                level = 0;
                break;
        }

        gpio_set_level(STATUS_LED_GPIO, level);

        ticks++;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void status_led_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    gpio_set_level(STATUS_LED_GPIO, 0);

    BaseType_t result = xTaskCreate(
        status_led_task,
        "status_led",
        2048,
        NULL,
        2,
        NULL
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "No se pudo crear la tarea del LED");
    }
}

void status_led_set_mode(status_led_mode_t mode)
{
    current_mode = mode;
}