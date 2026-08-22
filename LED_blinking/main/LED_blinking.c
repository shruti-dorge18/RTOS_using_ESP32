#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_GPIO 2
#define BLINK_DELAY 3000

void app_main(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    while (1)
    {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(BLINK_DELAY / portTICK_PERIOD_MS);

        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(BLINK_DELAY / portTICK_PERIOD_MS);
    }
}
