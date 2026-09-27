#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define notification_delay 2000

TaskHandle_t task1_handle;
TaskHandle_t task2_handle;

void task_1(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(notification_delay));
        printf("Task 1: Sending notification to Task 2\n");
        xTaskNotify(task2_handle, 5, eSetValueWithOverwrite);
    }
}

void task_2(void *pvParameters)
{
}

void app_main(void)
{

    xTaskCreate(task_1,       // function name
                "Task 1",     // task name (name does not control how the task executes)
                2048,         // stack size in bytes
                NULL,         // no data or argument to pass
                1,            // higher number = higher priority
                &task1_handle // task handle
    );

    xTaskCreate(task_2,
                "Task 2",
                2048,
                NULL,
                1,
                &task2_handle);
}
