#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_GPIO 2
#define BLINK_DELAY 50

void app_main(void)
{

    ledc_timer_config_t ledc_timer = // how pwm is generated
        {
            .freq_hz = 5000, // signal repeats 5000 times per second
            .duty_resolution = LEDC_TIMER_8_BIT,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = LEDC_TIMER_0, // using timer 0
        };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = // defines where pwm goes
        {
            .gpio_num = LED_GPIO,              // output to gpio 2
            .channel = LEDC_CHANNEL_0,         // using channel 0
            .timer_sel = LEDC_TIMER_0,         // connecting channel 0 to timer 0
            .duty = 0,                         // when the program starts,how bright should the led be? Represent that using duty. duty=0 means the led is off, duty=255 means the led is at its brightest.
            .hpoint = 0,                      
            .speed_mode = LEDC_LOW_SPEED_MODE, // must matched with the one used in timer configuration
        };

    ledc_channel_config(&ledc_channel);

    int duty = 0;
    int step = 5;

    while (1)
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        duty += step;

        if (duty >= 255 || duty <= 0)
        {
            step = -step;
        }

        printf("Duty = %d\n", duty);

        vTaskDelay(BLINK_DELAY / portTICK_PERIOD_MS);
    }
}
