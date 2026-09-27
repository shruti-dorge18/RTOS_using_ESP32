#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NOTIFICATION_COUNT 3
#define NOTIFICATION_DELAY 2000

TaskHandle_t task1_handle;
TaskHandle_t task2_handle;

void task_1(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(NOTIFICATION_DELAY));

        printf("Task 1: Sending %d notifications to Task 2\n", NOTIFICATION_COUNT);

        // Sending multiple notifications
        for (int i = 0; i < NOTIFICATION_COUNT; i++)
        {
            xTaskNotifyGive(task2_handle);
        }
    }
}

void task_2(void *pvParameters)
{
    while (1)
    {
        printf("Task 2: Waiting for notification from Task 1\n"); // This would execute first as Task1 has delay

        ulTaskNotifyTake(pdFALSE, portMAX_DELAY); // pdFALSE : decreases the notification count by 1
                                                  // pdTRUE : clears the notification count to 0

        printf("Task 2: Notification received from task 1\n");
    }
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