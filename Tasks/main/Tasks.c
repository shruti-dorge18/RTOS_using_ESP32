#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED_GPIO 2
#define LED_DELAY 1000

TaskHandle_t task1_handle;
TaskHandle_t task2_handle;

void task_1(void *pvParameters)
{
    while (1)
    {
        gpio_set_level(LED_GPIO, 1);
        printf("Led ON\n");
        vTaskDelay(LED_DELAY / portTICK_PERIOD_MS);

        gpio_set_level(LED_GPIO, 0);
        printf("Led OFF\n");
        vTaskDelay(LED_DELAY / portTICK_PERIOD_MS);
    }
}

void task_2(void *pvParameters)
{
    
    printf("Task 2 is running!\n");
    vTaskDelay(pdMS_TO_TICKS(1000));

        // Get Task 1 priority
    printf("Task 1 priority is %u\n", uxTaskPriorityGet(task1_handle));

    // Change Task 1 priority
    vTaskPrioritySet(task1_handle, 3);

    printf("Task 1 new priority is %u\n", uxTaskPriorityGet(task1_handle));

    // Get Task 2's own priority
    printf("Task 2 priority is %u\n", uxTaskPriorityGet(NULL));

    // Change Task 2's own priority
    vTaskPrioritySet(NULL, 3);

    printf("Task 2 new priority is %u\n", uxTaskPriorityGet(NULL));

    // Suspend Task 1
    printf("Suspending Task 1\n");
    vTaskSuspend(task1_handle);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Resume Task 1
    printf("Resuming Task 1\n");
    vTaskResume(task1_handle);

    // Give Task 1 some time to execute
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Delete Task 1
    printf("Deleting Task 1\n");
    vTaskDelete(task1_handle);
    task1_handle = NULL;

    // Delete Task 2 itself
    printf("Deleting Task 2\n");
    vTaskDelete(NULL);  
   
}

void app_main(void)
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

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