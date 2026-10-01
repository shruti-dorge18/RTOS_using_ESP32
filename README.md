## 🚀 About this Repository

A practical learning repository for Real-Time Operating System (RTOS) concepts, implemented with **FreeRTOS** on the ESP32.

## 📌 Note

This repository demonstrates **FreeRTOS concepts using ESP32 and ESP-IDF**.

The core **FreeRTOS concepts and APIs** introduced here can generally be used across microcontrollers that support FreeRTOS. However, **hardware-specific APIs and peripheral configuration** depend on the microcontroller and its development framework.

| Example                      | Scope                    |
| ---------------------------- | ------------------------ |
| `xTaskCreate()`              | FreeRTOS API             |
| `vTaskDelay()`               | FreeRTOS API             |
| Task scheduling & priorities | FreeRTOS concepts        |
| `gpio_set_level()`           | ESP32 / ESP-IDF specific |
| ESP32 Wi-Fi APIs             | ESP32 / ESP-IDF specific |

> 💡 **In short:** The RTOS concepts learned here can be applied to other **FreeRTOS-based microcontrollers**, but hardware-specific code may need to be adapted to the target platform.

## ⚙️ What is RTOS?

**RTOS (Real-Time Operating System)** is an operating system designed to manage multiple tasks while ensuring that important tasks get the required CPU time at the right moment.

### A simple example

Consider a **drone** flying in the air.

At the same time, the system needs to:

* Read data from sensors
* Control the motors
* Receive commands from the remote
* Monitor the battery
* Send telemetry data

These are different tasks running on the same microcontroller.

An RTOS helps organize these tasks by deciding **which task should run, when it should run, and how long it can use the CPU**.

For example, **motor control** may need to run more frequently than sending telemetry data. The RTOS scheduler can give the motor-control task the required CPU time while allowing the other tasks to run as well.

This makes it easier to build embedded systems where **multiple operations need to happen at the same time and within predictable timing requirements**.

---

## Where is RTOS Used?

RTOS is used in embedded systems that need to handle **multiple tasks with timing requirements**.

Some common applications are:

| Application             | Example RTOS Tasks                                |
| ----------------------- | ------------------------------------------------- |
| 🤖 Robotics             | Motor control, sensor reading, communication      |
| 🚗 Automotive           | Sensor processing, control systems, communication |
| 🛩️ Drones              | Flight control, sensors, motors, telemetry        |
| 🏭 Industrial systems   | Machine control, monitoring, communication        |
| 📡 IoT devices          | Sensor data, networking, device control           |
| 🏠 Smart home devices   | Sensors, communication, user input                |
| ❤️ Medical devices      | Sensor monitoring, display, alarms                |
| 📱 Consumer electronics | Input handling, display, communication            |

