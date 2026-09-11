#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
// #include "mqtt_client.h"
// #include "esp_crt_bundle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

#include "esp_adc/adc_oneshot.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_rom_sys.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#define TAG "ECOAIR"

// #define MQTT_BROKER_URI
// "9937393955b843879fe2173d35347886.s1.eu.hivemq.cloud:8883"
// #define MQTT_USERNAME       "YOUR_USERNAME"
// #define MQTT_PASSWORD       "YOUR_PASSWORD"

// #define MQTT_COMMAND_TOPIC  "ecoair/device001/command"
// #define MQTT_STATUS_TOPIC   "ecoair/device001/status"

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

#define OLED_SDA_GPIO           GPIO_NUM_4
#define OLED_SCL_GPIO           GPIO_NUM_5

#define SPEED_TOUCH_GPIO        GPIO_NUM_12
#define AUTO_TOUCH_GPIO         GPIO_NUM_15
#define TIMER_TOUCH_GPIO        GPIO_NUM_23

#define SERVO_GPIO              GPIO_NUM_18
#define BUZZER_GPIO             GPIO_NUM_19

#define DUST_LED_GPIO           GPIO_NUM_25
#define DUST_ADC_CHANNEL        ADC_CHANNEL_6

#define OLED_I2C_ADDRESS        0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64

#define SERVO_FREQUENCY         50

#define SERVO_OFF_DEG           0
#define SERVO_LOW_DEG           95
#define SERVO_MEDIUM_DEG        120
#define SERVO_HIGH_DEG          140

#define SERVO_MIN_PULSE_US      500
#define SERVO_MAX_PULSE_US      2500

#define AUTO_SAMPLES            60
#define DUST_SAMPLE_INTERVAL_MS 1000

#define DUST_LED_ON_US          280
#define DUST_ADC_WAIT_US        40

#define DUST_LOW_LIMIT          35.0f
#define DUST_MEDIUM_LIMIT       75.0f
#define DUST_HIGH_LIMIT         500.0f

#define TIMER_30_MINUTES        30
#define TIMER_60_MINUTES        60

#define AUTO_RUNTIME_MINUTES    60

#define WIFI_SSID               "Pawan"
#define WIFI_PASSWORD           "12345678"

#define STATIC_IP               "10.16.90.50"
#define GATEWAY_IP              "10.16.90.203"
#define SUBNET_MASK             "255.255.255.0"
#define DNS_IP                  "8.8.8.8"

#define HTTP_SERVER_PORT        80

typedef enum
{
    FAN_OFF = 0,
    FAN_LOW,
    FAN_MEDIUM,
    FAN_HIGH
} fan_speed_t;

typedef enum
{
    AQI_LOW = 0,
    AQI_MEDIUM,
    AQI_HIGH
} aqi_level_t;

typedef enum
{
    DISPLAY_NORMAL = 0,
    DISPLAY_TIMER,
    DISPLAY_AUTO
} display_mode_t;

static fan_speed_t current_fan_speed = FAN_OFF;

static aqi_level_t current_aqi = AQI_LOW;

static float current_dust = 0.0f;

static bool power_state = false;

static bool timer_active = false;

static int timer_minutes = 0;

static uint64_t timer_end_time = 0;

static bool auto_mode = false;

static bool auto_measuring = false;

static uint64_t auto_end_time = 0;

static int auto_sample_count = 0;

static float auto_dust_sum = 0.0f;

static uint64_t next_auto_sample_time = 0;

static bool sleep_mode = false;

static bool wifi_connected = false;

static httpd_handle_t web_server = NULL;

static display_mode_t display_mode = DISPLAY_NORMAL;

static i2c_master_bus_handle_t oled_bus_handle = NULL;

static i2c_master_dev_handle_t oled_dev_handle = NULL;
static void oled_set_position(int page, int column);
static void oled_data(const uint8_t *data, size_t len);

static uint8_t oled_frame[OLED_WIDTH * OLED_HEIGHT / 8];
static uint8_t oled_last_frame[OLED_WIDTH * OLED_HEIGHT / 8];
static bool oled_frame_initialized = false;

static adc_oneshot_unit_handle_t adc_handle = NULL;

static const uint8_t font5x7[][5] =
{

    {0x00,0x00,0x00,0x00,0x00},

    {0x7E,0x11,0x11,0x11,0x7E},

    {0x7F,0x49,0x49,0x49,0x36},

    {0x3E,0x41,0x41,0x41,0x22},

    {0x7F,0x41,0x41,0x22,0x1C},

    {0x7F,0x49,0x49,0x49,0x41},

    {0x7F,0x09,0x09,0x09,0x01},

    {0x3E,0x41,0x49,0x49,0x7A},

    {0x7F,0x08,0x08,0x08,0x7F},

    {0x00,0x41,0x7F,0x41,0x00},

    {0x20,0x40,0x41,0x3F,0x01},

    {0x7F,0x08,0x14,0x22,0x41},

    {0x7F,0x40,0x40,0x40,0x40},

    {0x7F,0x02,0x0C,0x02,0x7F},

    {0x7F,0x04,0x08,0x10,0x7F},

    {0x3E,0x41,0x41,0x41,0x3E},

    {0x7F,0x09,0x09,0x09,0x06},

    {0x3E,0x41,0x51,0x21,0x5E},

    {0x7F,0x09,0x19,0x29,0x46},

    {0x46,0x49,0x49,0x49,0x31},

    {0x01,0x01,0x7F,0x01,0x01},

    {0x3F,0x40,0x40,0x40,0x3F},

    {0x1F,0x20,0x40,0x20,0x1F},

    {0x3F,0x40,0x38,0x40,0x3F},

    {0x63,0x14,0x08,0x14,0x63},

    {0x07,0x08,0x70,0x08,0x07},

    {0x61,0x51,0x49,0x45,0x43},

    {0x3E,0x51,0x49,0x45,0x3E},

    {0x00,0x42,0x7F,0x40,0x00},

    {0x42,0x61,0x51,0x49,0x46},

    {0x21,0x41,0x45,0x4B,0x31},

    {0x18,0x14,0x12,0x7F,0x10},

    {0x27,0x45,0x45,0x45,0x39},

    {0x3C,0x4A,0x49,0x49,0x30},

    {0x01,0x71,0x09,0x05,0x03},

    {0x36,0x49,0x49,0x49,0x36},

    {0x06,0x49,0x49,0x29,0x1E},

    {0x00,0x36,0x36,0x00,0x00},

    {0x08,0x08,0x08,0x08,0x08},

    {0x00,0x60,0x60,0x00,0x00},

    {0x20,0x10,0x08,0x04,0x02},

    {0x63,0x13,0x08,0x64,0x63}
};

static int font_index(char c)
{
    if (c == ' ')
        return 0;

    if (c >= 'A' && c <= 'Z')
        return 1 + (c - 'A');

    if (c >= '0' && c <= '9')
        return 27 + (c - '0');

    if (c == ':')
        return 37;

    if (c == '-')
        return 38;

    if (c == '.')
        return 39;

    if (c == '/')
        return 40;

    if (c == '%')
        return 41;

    return 0;
}

static void oled_frame_clear(void)
{
    memset(oled_frame, 0, sizeof(oled_frame));
}

static void oled_frame_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    oled_frame[x + (y / 8) * OLED_WIDTH] |= (uint8_t)(1U << (y & 7));
}

static void oled_frame_char(int x, int y, char c, int scale)
{
    int index = font_index(c);
    if (scale < 1) scale = 1;

    for (int col = 0; col < 5; col++)
    {
        uint8_t bits = font5x7[index][col];
        for (int row = 0; row < 7; row++)
        {
            if (bits & (1U << row))
            {
                for (int dx = 0; dx < scale; dx++)
                    for (int dy = 0; dy < scale; dy++)
                        oled_frame_pixel(x + col * scale + dx,
                                         y + row * scale + dy);
            }
        }
    }
}

static void oled_frame_text(int x, int y, const char *text, int scale)
{
    while (*text)
    {
        oled_frame_char(x, y, *text++, scale);
        x += 6 * scale;
    }
}

static void oled_frame_update(void)
{
    if (oled_dev_handle == NULL)
        return;

    if (oled_frame_initialized &&
        memcmp(oled_frame, oled_last_frame, sizeof(oled_frame)) == 0)
        return;

    for (int page = 0; page < 8; page++)
    {
        oled_set_position(page, 0);
        oled_data(&oled_frame[page * OLED_WIDTH], OLED_WIDTH);
    }

    memcpy(oled_last_frame, oled_frame, sizeof(oled_frame));
    oled_frame_initialized = true;
}

static void oled_command(uint8_t command)
{
    uint8_t data[2];

    data[0] = 0x00;
    data[1] = command;

    if (oled_dev_handle != NULL)
    {
        ESP_ERROR_CHECK(
            i2c_master_transmit(
                oled_dev_handle,
                data,
                sizeof(data),
                -1
            )
        );
    }
}

static void oled_data(const uint8_t *data, size_t len)
{
    uint8_t buffer[129];

    if (len > 128)
        len = 128;

    buffer[0] = 0x40;

    memcpy(
        &buffer[1],
        data,
        len
    );

    if (oled_dev_handle != NULL)
    {
        ESP_ERROR_CHECK(
            i2c_master_transmit(
                oled_dev_handle,
                buffer,
                len + 1,
                -1
            )
        );
    }
}

static void oled_set_position(
    int page,
    int column
)
{
    oled_command(
        0xB0 + page
    );

    oled_command(
        0x00 + (column & 0x0F)
    );

    oled_command(
        0x10 + ((column >> 4) & 0x0F)
    );
}

static void oled_init(void)
{
    i2c_master_bus_config_t bus_config =
    {
        .clk_source = I2C_CLK_SRC_DEFAULT,

        .i2c_port = I2C_NUM_0,

        .sda_io_num = OLED_SDA_GPIO,

        .scl_io_num = OLED_SCL_GPIO,

        .glitch_ignore_cnt = 7,

        .flags.enable_internal_pullup = true
    };

    ESP_ERROR_CHECK(
        i2c_new_master_bus(
            &bus_config,
            &oled_bus_handle
        )
    );

    i2c_device_config_t dev_config =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,

        .device_address = OLED_I2C_ADDRESS,

        .scl_speed_hz = 400000
    };

    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(
            oled_bus_handle,
            &dev_config,
            &oled_dev_handle
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    oled_command(0xAE);
    oled_command(0xD5);
    oled_command(0x80);

    oled_command(0xA8);
    oled_command(0x3F);

    oled_command(0xD3);
    oled_command(0x00);

    oled_command(0x40);

    oled_command(0x8D);
    oled_command(0x14);

    oled_command(0x20);
    oled_command(0x00);

    oled_command(0xA1);

    oled_command(0xC8);

    oled_command(0xDA);
    oled_command(0x12);

    oled_command(0x81);
    oled_command(0xCF);

    oled_command(0xD9);
    oled_command(0xF1);

    oled_command(0xDB);
    oled_command(0x40);

    oled_command(0xA4);

    oled_command(0xA6);

    oled_command(0xAF);

    ESP_LOGI(
        TAG,
        "OLED initialized"
    );
}

static void oled_clear(void)
{
    uint8_t blank[128];

    memset(
        blank,
        0,
        sizeof(blank)
    );

    for (int page = 0; page < 8; page++)
    {
        oled_set_position(
            page,
            0
        );

        oled_data(
            blank,
            128
        );
    }
}

static const char *fan_text(
    fan_speed_t speed
)
{
    switch (speed)
    {
        case FAN_LOW:
            return "LOW";

        case FAN_MEDIUM:
            return "MEDIUM";

        case FAN_HIGH:
            return "HIGH";

        case FAN_OFF:
        default:
            return "OFF";
    }
}

static const char *aqi_text(
    aqi_level_t level
)
{
    switch (level)
    {
        case AQI_MEDIUM:
            return "MEDIUM";

        case AQI_HIGH:
            return "HIGH";

        case AQI_LOW:
        default:
            return "LOW";
    }
}

static aqi_level_t determine_air_quality(
    float dust
)
{
    if (dust <= DUST_LOW_LIMIT)
        return AQI_LOW;

    if (dust <= DUST_MEDIUM_LIMIT)
        return AQI_MEDIUM;

    return AQI_HIGH;
}

static void oled_show_status(void)
{
    char line[32];

    oled_frame_clear();

    oled_frame_text(0, 0, "AQI:", 1);
    oled_frame_text(30, 0, aqi_text(current_aqi), 2);

    oled_frame_text(0, 18, "FAN:", 1);
    oled_frame_text(30, 17, fan_text(current_fan_speed), 2);

    snprintf(line, sizeof(line), "DUST:%.1f", current_dust);
    oled_frame_text(0, 34, line, 1);

    if (display_mode == DISPLAY_AUTO && auto_mode)
    {
        if (auto_measuring)
            snprintf(line, sizeof(line), "AUTO:%d/60", auto_sample_count);
        else
            snprintf(line, sizeof(line), "AUTO ON");

        oled_frame_text(0, 46, line, 1);
    }
    else if (timer_active)
    {
        snprintf(line, sizeof(line), "TIMER:%dM", timer_minutes);
        oled_frame_text(0, 46, line, 1);
    }
    else
    {
        oled_frame_text(0, 46, "MANUAL", 1);
    }

    oled_frame_update();
}

static void buzzer_init(void)
{
    gpio_config_t config =
    {
        .pin_bit_mask =
            (1ULL << BUZZER_GPIO),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(
            &config
        )
    );

    gpio_set_level(
        BUZZER_GPIO,
        0
    );
}

static void buzzer_beep(void)
{
    gpio_set_level(
        BUZZER_GPIO,
        1
    );

    vTaskDelay(
        pdMS_TO_TICKS(80)
    );

    gpio_set_level(
        BUZZER_GPIO,
        0
    );
}

static uint32_t servo_angle_to_duty(
    int angle
)
{
    int pulse_us;

    pulse_us =
        SERVO_MIN_PULSE_US +
        (
            (
                SERVO_MAX_PULSE_US -
                SERVO_MIN_PULSE_US
            ) *
            angle
        ) / 180;

    uint32_t duty =
        (
            (uint32_t)pulse_us *
            65535
        ) / 20000;

    return duty;
}

static void servo_set_angle(
    int angle
)
{
    if (angle < 0)
        angle = 0;

    if (angle > 180)
        angle = 180;

    uint32_t duty =
        servo_angle_to_duty(
            angle
        );

    ESP_ERROR_CHECK(
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        )
    );

    ESP_ERROR_CHECK(
        ledc_update_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0
        )
    );

    ESP_LOGI(
        TAG,
        "SERVO ANGLE = %d degrees",
        angle
    );
}

static void servo_init(void)
{
    ledc_timer_config_t timer_config =
    {
        .speed_mode =
            LEDC_LOW_SPEED_MODE,

        .timer_num =
            LEDC_TIMER_0,

        .duty_resolution =
            LEDC_TIMER_16_BIT,

        .freq_hz =
            SERVO_FREQUENCY,

        .clk_cfg =
            LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(
        ledc_timer_config(
            &timer_config
        )
    );

    ledc_channel_config_t channel_config =
    {
        .gpio_num =
            SERVO_GPIO,

        .speed_mode =
            LEDC_LOW_SPEED_MODE,

        .channel =
            LEDC_CHANNEL_0,

        .intr_type =
            LEDC_INTR_DISABLE,

        .timer_sel =
            LEDC_TIMER_0,

        .duty =
            0,

        .hpoint =
            0
    };

    ESP_ERROR_CHECK(
        ledc_channel_config(
            &channel_config
        )
    );

    servo_set_angle(
        SERVO_OFF_DEG
    );

    ESP_LOGI(
        TAG,
        "MG90S servo initialized on GPIO %d",
        SERVO_GPIO
    );
}

static void set_fan_speed(
    fan_speed_t speed
)
{
    current_fan_speed =
        speed;

    switch (speed)
    {
        case FAN_OFF:

            servo_set_angle(
                SERVO_OFF_DEG
            );

            break;

        case FAN_LOW:

            servo_set_angle(
                SERVO_LOW_DEG
            );

            break;

        case FAN_MEDIUM:

            servo_set_angle(
                SERVO_MEDIUM_DEG
            );

            break;

        case FAN_HIGH:

            servo_set_angle(
                SERVO_HIGH_DEG
            );

            break;
    }

    ESP_LOGI(
        TAG,
        "FAN SPEED = %s",
        fan_text(speed)
    );
}

static void dust_sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config =
    {
        .unit_id = ADC_UNIT_1
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(
            &init_config,
            &adc_handle
        )
    );

    adc_oneshot_chan_cfg_t channel_config =
    {
        .bitwidth =
            ADC_BITWIDTH_DEFAULT,

        .atten =
            ADC_ATTEN_DB_12
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            DUST_ADC_CHANNEL,
            &channel_config
        )
    );

    gpio_config_t led_config =
    {
        .pin_bit_mask =
            (1ULL << DUST_LED_GPIO),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(
            &led_config
        )
    );

    gpio_set_level(
        DUST_LED_GPIO,
        0
    );

    ESP_LOGI(
        TAG,
        "Dust sensor initialized"
    );
}

static float read_dust_density(void)
{
    int raw = 0;

    gpio_set_level(
        DUST_LED_GPIO,
        1
    );

    esp_rom_delay_us(
        DUST_LED_ON_US
    );

    esp_rom_delay_us(
        DUST_ADC_WAIT_US
    );

    ESP_ERROR_CHECK(
        adc_oneshot_read(
            adc_handle,
            DUST_ADC_CHANNEL,
            &raw
        )
    );

    gpio_set_level(
        DUST_LED_GPIO,
        0
    );

    float adc_voltage =
        ((float)raw / 4095.0f) * 3.3f;

    float sensor_voltage =
        adc_voltage * 1.5f;

    float dust =
        (
            sensor_voltage - 0.60f
        ) / 0.50f * 1000.0f;

    if (dust < 0.0f)
        dust = 0.0f;

    if (dust > 1000.0f)
        dust = 1000.0f;

    return dust;
}

static void update_dust_reading(void)
{
    current_dust =
        read_dust_density();

    current_aqi =
        determine_air_quality(
            current_dust
        );

    ESP_LOGI(
        TAG,
        "DUST = %.2f ug/m3 | AQI = %s",
        current_dust,
        aqi_text(current_aqi)
    );
}

static void touch_init(void)
{
    gpio_config_t config =
    {
        .pin_bit_mask =
            (
                (1ULL << SPEED_TOUCH_GPIO) |
                (1ULL << AUTO_TOUCH_GPIO) |
                (1ULL << TIMER_TOUCH_GPIO)
            ),

        .mode =
            GPIO_MODE_INPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(
            &config
        )
    );

    ESP_LOGI(
        TAG,
        "Touch buttons initialized"
    );
}

static void cancel_auto_mode(void)
{
    auto_mode = false;

    auto_measuring = false;

    auto_sample_count = 0;

    auto_dust_sum = 0.0f;

    auto_end_time = 0;

    next_auto_sample_time = 0;
}

static void cancel_timer(void)
{
    timer_active = false;

    timer_minutes = 0;

    timer_end_time = 0;
}

static void start_auto_measurement(void)
{

    auto_mode = true;

    auto_measuring = true;

    auto_sample_count = 0;

    auto_dust_sum = 0.0f;

    auto_end_time = 0;

    next_auto_sample_time = 0;

    display_mode =
        DISPLAY_AUTO;

    ESP_LOGI(
        TAG,
        "AUTO MODE STARTED"
    );

    ESP_LOGI(
        TAG,
        "Measuring dust for 60 seconds..."
    );
}

static void process_auto_measurement(void)
{
    if (!auto_mode ||
        !auto_measuring)
    {
        return;
    }

    uint64_t now =
        esp_timer_get_time() /
        1000ULL;

    if (
        auto_sample_count == 0 &&
        next_auto_sample_time == 0
    )
    {
        float dust =
            read_dust_density();

        auto_dust_sum += dust;

        auto_sample_count++;

        current_dust =
            dust;

        current_aqi =
            determine_air_quality(
                dust
            );

        next_auto_sample_time =
            now +
            DUST_SAMPLE_INTERVAL_MS;

        ESP_LOGI(
            TAG,
            "AUTO SAMPLE %d/60 = %.2f",
            auto_sample_count,
            dust
        );

        oled_show_status();

        return;
    }

    if (
        auto_sample_count <
        AUTO_SAMPLES &&
        now >= next_auto_sample_time
    )
    {
        float dust =
            read_dust_density();

        auto_dust_sum += dust;

        auto_sample_count++;

        current_dust =
            dust;

        current_aqi =
            determine_air_quality(
                dust
            );

        next_auto_sample_time =
            now +
            DUST_SAMPLE_INTERVAL_MS;

        ESP_LOGI(
            TAG,
            "AUTO SAMPLE %d/60 = %.2f",
            auto_sample_count,
            dust
        );

        oled_show_status();
    }

    if (
        auto_sample_count >=
        AUTO_SAMPLES
    )
    {
        float average_dust =
            auto_dust_sum /
            (float)AUTO_SAMPLES;

        current_dust =
            average_dust;

        current_aqi =
            determine_air_quality(
                average_dust
            );

        ESP_LOGI(
            TAG,
            "========================================"
        );

        ESP_LOGI(
            TAG,
            "AUTO MEASUREMENT COMPLETE"
        );

        ESP_LOGI(
            TAG,
            "AVERAGE DUST = %.2f ug/m3",
            average_dust
        );

        ESP_LOGI(
            TAG,
            "AIR QUALITY = %s",
            aqi_text(current_aqi)
        );

        if (
            average_dust <=
            DUST_LOW_LIMIT
        )
        {
            set_fan_speed(
                FAN_LOW
            );
        }

        else if (
            average_dust <=
            DUST_MEDIUM_LIMIT
        )
        {
            set_fan_speed(
                FAN_MEDIUM
            );
        }

        else
        {
            set_fan_speed(
                FAN_HIGH
            );
        }

        auto_measuring = false;

        auto_end_time =
            (
                esp_timer_get_time() /
                1000000ULL
            ) +
            (
                AUTO_RUNTIME_MINUTES *
                60ULL
            );

        ESP_LOGI(
            TAG,
            "AUTO FAN = %s",
            fan_text(
                current_fan_speed
            )
        );

        ESP_LOGI(
            TAG,
            "AUTO RUNTIME = 60 MINUTES"
        );

        oled_show_status();
    }
}

static void process_auto_runtime(void)
{
    if (
        !auto_mode ||
        auto_measuring
    )
    {
        return;
    }

    if (auto_end_time == 0)
        return;

    uint64_t now =
        esp_timer_get_time() /
        1000000ULL;

    if (now >= auto_end_time)
    {
        ESP_LOGI(
            TAG,
            "AUTO MODE COMPLETE"
        );

        auto_mode = false;

        auto_measuring = false;

        auto_end_time = 0;

        set_fan_speed(
            FAN_MEDIUM
        );

        display_mode =
            DISPLAY_NORMAL;

        oled_show_status();
    }
}

static void set_timer_minutes(
    int minutes
)
{
    if (
        minutes !=
        TIMER_30_MINUTES &&
        minutes !=
        TIMER_60_MINUTES
    )
    {
        return;
    }

    cancel_auto_mode();

    timer_active = true;

    timer_minutes =
        minutes;

    timer_end_time =
        (
            esp_timer_get_time() /
            1000000ULL
        ) +
        (
            (uint64_t)minutes *
            60ULL
        );

    display_mode =
        DISPLAY_TIMER;

    if (
        current_fan_speed ==
        FAN_OFF
    )
    {
        set_fan_speed(
            FAN_MEDIUM
        );
    }

    ESP_LOGI(
        TAG,
        "TIMER STARTED = %d MINUTES",
        minutes
    );

    oled_show_status();
}

static void process_timer(void)
{
    if (!timer_active)
        return;

    if (timer_end_time == 0)
        return;

    uint64_t now =
        esp_timer_get_time() /
        1000000ULL;

    if (now >= timer_end_time)
    {
        ESP_LOGI(
            TAG,
            "TIMER COMPLETE"
        );

        timer_active = false;

        timer_minutes = 0;

        timer_end_time = 0;

        set_fan_speed(
            FAN_OFF
        );

        display_mode =
            DISPLAY_NORMAL;

        oled_show_status();
    }
}

static void handle_speed_button(void)
{

    cancel_auto_mode();

    cancel_timer();

    display_mode =
        DISPLAY_NORMAL;

    switch (current_fan_speed)
    {
        case FAN_OFF:

            set_fan_speed(
                FAN_LOW
            );

            break;

        case FAN_LOW:

            set_fan_speed(
                FAN_MEDIUM
            );

            break;

        case FAN_MEDIUM:

            set_fan_speed(
                FAN_HIGH
            );

            break;

        case FAN_HIGH:

            set_fan_speed(
                FAN_OFF
            );

            break;
    }

    buzzer_beep();

    oled_show_status();
}

static void handle_auto_button(void)
{

    cancel_timer();

    start_auto_measurement();

    buzzer_beep();

    oled_show_status();
}

static void handle_timer_button(void)
{

    if (!timer_active)
    {
        set_timer_minutes(
            TIMER_30_MINUTES
        );
    }

    else if (
        timer_minutes ==
        TIMER_30_MINUTES
    )
    {
        set_timer_minutes(
            TIMER_60_MINUTES
        );
    }

    else
    {
        cancel_timer();

        display_mode =
            DISPLAY_NORMAL;

        oled_show_status();
    }

    buzzer_beep();
}

static void set_power(
    bool on
)
{
    power_state = on;

    if (!on)
    {

        set_fan_speed(
            FAN_OFF
        );

        cancel_timer();

        cancel_auto_mode();
    }

    else
    {

        if (
            current_fan_speed ==
            FAN_OFF
        )
        {
            set_fan_speed(
                FAN_MEDIUM
            );
        }
    }

    display_mode =
        DISPLAY_NORMAL;

    oled_show_status();
}

static void set_sleep_mode(
    bool enabled
)
{
    sleep_mode =
        enabled;

    if (sleep_mode)
    {

        set_fan_speed(
            FAN_LOW
        );
    }

    display_mode =
        DISPLAY_NORMAL;

    oled_show_status();
}

static void button_task(
    void *arg
)
{
    int old_speed =
        gpio_get_level(
            SPEED_TOUCH_GPIO
        );

    int old_auto =
        gpio_get_level(
            AUTO_TOUCH_GPIO
        );

    int old_timer =
        gpio_get_level(
            TIMER_TOUCH_GPIO
        );

    while (1)
    {
        int speed_level =
            gpio_get_level(
                SPEED_TOUCH_GPIO
            );

        int auto_level =
            gpio_get_level(
                AUTO_TOUCH_GPIO
            );

        int timer_level =
            gpio_get_level(
                TIMER_TOUCH_GPIO
            );

        if (
            speed_level == 1 &&
            old_speed == 0
        )
        {
            ESP_LOGI(
                TAG,
                "SPEED TOUCH DETECTED"
            );

            handle_speed_button();

            vTaskDelay(
                pdMS_TO_TICKS(250)
            );

            speed_level =
                gpio_get_level(
                    SPEED_TOUCH_GPIO
                );
        }

        if (
            auto_level == 1 &&
            old_auto == 0
        )
        {
            ESP_LOGI(
                TAG,
                "AUTO TOUCH DETECTED"
            );

            handle_auto_button();

            vTaskDelay(
                pdMS_TO_TICKS(250)
            );

            auto_level =
                gpio_get_level(
                    AUTO_TOUCH_GPIO
                );
        }

        if (
            timer_level == 1 &&
            old_timer == 0
        )
        {
            ESP_LOGI(
                TAG,
                "TIMER TOUCH DETECTED"
            );

            handle_timer_button();

            vTaskDelay(
                pdMS_TO_TICKS(250)
            );

            timer_level =
                gpio_get_level(
                    TIMER_TOUCH_GPIO
                );
        }

        old_speed =
            speed_level;

        old_auto =
            auto_level;

        old_timer =
            timer_level;

        vTaskDelay(
            pdMS_TO_TICKS(30)
        );
    }
}

static void purifier_task(
    void *arg
)
{
    uint64_t last_dust_read =
        0;

    uint64_t last_oled_update =
        0;

    while (1)
    {
        uint64_t now_ms =
            esp_timer_get_time() /
            1000ULL;

        if (!auto_measuring)
        {
            if (
                now_ms -
                last_dust_read >=
                2000
            )
            {
                update_dust_reading();

                last_dust_read =
                    now_ms;
            }
        }

        process_auto_measurement();

        process_timer();

        process_auto_runtime();

        if (
            now_ms -
            last_oled_update >=
            1000
        )
        {
            oled_show_status();

            last_oled_update =
                now_ms;
        }

        vTaskDelay(
            pdMS_TO_TICKS(50)
        );
    }
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{

    if (
        event_base == WIFI_EVENT &&
        event_id ==
            WIFI_EVENT_STA_START
    )
    {
        ESP_LOGI(
            TAG,
            "Wi-Fi started"
        );

        ESP_LOGI(
            TAG,
            "Connecting to: %s",
            WIFI_SSID
        );

        esp_wifi_connect();
    }

    else if (
        event_base == WIFI_EVENT &&
        event_id ==
            WIFI_EVENT_STA_DISCONNECTED
    )
    {
        wifi_connected =
            false;

        ESP_LOGW(
            TAG,
            "Wi-Fi disconnected"
        );

        ESP_LOGI(
            TAG,
            "Trying to reconnect..."
        );

        esp_wifi_connect();
    }

    else if (
        event_base == IP_EVENT &&
        event_id ==
            IP_EVENT_STA_GOT_IP
    )
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        wifi_connected =
            true;

        ESP_LOGI(
            TAG,
            "========================================"
        );

        ESP_LOGI(
            TAG,
            "       WIFI CONNECTED"
        );

        ESP_LOGI(
            TAG,
            "ESP32 IP ADDRESS: " IPSTR,
            IP2STR(
                &event->ip_info.ip
            )
        );

        ESP_LOGI(
            TAG,
            "========================================"
        );

        ESP_LOGI(
            TAG,
            "API URL:"
        );

        ESP_LOGI(
            TAG,
            "http://" IPSTR,
            IP2STR(
                &event->ip_info.ip
            )
        );
    }
}

static void wifi_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing Wi-Fi..."
    );

    ESP_ERROR_CHECK(
        esp_netif_init()
    );

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_t *wifi_netif =
        esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(
            &cfg
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config =
    {
        .sta =
        {
            .ssid =
                WIFI_SSID,

            .password =
                WIFI_PASSWORD,

            .threshold.authmode =
                WIFI_AUTH_WPA2_PSK,

            .pmf_cfg =
            {
                .capable = true,

                .required = false
            }
        }
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(
            WIFI_MODE_STA
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    esp_netif_ip_info_t ip_info;

    ESP_ERROR_CHECK(
        esp_netif_dhcpc_stop(
            wifi_netif
        )
    );

    IP4_ADDR(
        &ip_info.ip,
        10, 16, 90, 50
    );

    IP4_ADDR(
        &ip_info.gw,
        10, 16, 90, 203
    );

    IP4_ADDR(
        &ip_info.netmask,
        255, 255, 255, 0
    );

    ESP_ERROR_CHECK(
        esp_netif_set_ip_info(
            wifi_netif,
            &ip_info
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(
        TAG,
        "Wi-Fi initialization complete"
    );

    ESP_LOGI(
        TAG,
        "Static IP configured:"
    );

    ESP_LOGI(
        TAG,
        "IP      = " STATIC_IP
    );

    ESP_LOGI(
        TAG,
        "Gateway = " GATEWAY_IP
    );

    ESP_LOGI(
        TAG,
        "Subnet  = " SUBNET_MASK
    );
}

static esp_err_t send_json_response(
    httpd_req_t *req,
    const char *response
)
{
    httpd_resp_set_type(
        req,
        "application/json"
    );

    httpd_resp_set_hdr(
        req,
        "Access-Control-Allow-Origin",
        "*"
    );

    return httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN
    );
}

static esp_err_t api_status_handler(
    httpd_req_t *req
)
{
    char response[512];

    snprintf(
        response,
        sizeof(response),

        "{"
        "\"connected\":%s,"
        "\"power\":%s,"
        "\"fan\":%d,"
        "\"fanLevel\":\"%s\","
        "\"mode\":\"%s\","
        "\"sleep\":%s,"
        "\"dust\":%.2f,"
        "\"airQuality\":\"%s\","
        "\"aqi\":\"%s\","
        "\"filter\":100,"
        "\"timer\":%d,"
        "\"autoMeasuring\":%s,"
        "\"servoAngle\":%d"
        "}",

        wifi_connected
            ? "true"
            : "false",

        power_state
            ? "true"
            : "false",

        (int)current_fan_speed,

        fan_text(
            current_fan_speed
        ),

        auto_mode
            ? "auto"
            : "manual",

        sleep_mode
            ? "true"
            : "false",

        current_dust,

        aqi_text(
            current_aqi
        ),

        aqi_text(
            current_aqi
        ),

        timer_active
            ? timer_minutes
            : 0,

        auto_measuring
            ? "true"
            : "false",

        (
            current_fan_speed ==
            FAN_OFF
        )
            ? SERVO_OFF_DEG
            :
        (
            current_fan_speed ==
            FAN_LOW
        )
            ? SERVO_LOW_DEG
            :
        (
            current_fan_speed ==
            FAN_MEDIUM
        )
            ? SERVO_MEDIUM_DEG
            :
            SERVO_HIGH_DEG
    );

    return send_json_response(
        req,
        response
    );
}

static esp_err_t api_fan_handler(
    httpd_req_t *req
)
{
    char body[128];

    if (
        req->content_len <= 0 ||
        req->content_len >=
            sizeof(body)
    )
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received = 0;

    int remaining =
        req->content_len;

    while (remaining > 0)
    {
        int ret =
            httpd_req_recv(
                req,
                body + received,
                remaining
            );

        if (ret <= 0)
        {
            httpd_resp_send_err(
                req,
                HTTPD_400_BAD_REQUEST,
                "Failed to read body"
            );

            return ESP_FAIL;
        }

        received += ret;

        remaining -= ret;
    }

    body[received] =
        '\0';

    ESP_LOGI(
        TAG,
        "FAN REQUEST: %s",
        body
    );

    char *speed_ptr =
        strstr(
            body,
            "\"speed\""
        );

    if (!speed_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing speed"
        );

        return ESP_FAIL;
    }

    speed_ptr =
        strchr(
            speed_ptr,
            ':'
        );

    if (!speed_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid speed"
        );

        return ESP_FAIL;
    }

    int speed = 0;

    sscanf(
        speed_ptr + 1,
        "%d",
        &speed
    );

    if (speed < 0)
        speed = 0;

    if (speed > 100)
        speed = 100;

    cancel_auto_mode();

    cancel_timer();

    display_mode =
        DISPLAY_NORMAL;

    if (speed == 0)
    {
        set_fan_speed(
            FAN_OFF
        );
    }

    else if (speed <= 33)
    {
        set_fan_speed(
            FAN_LOW
        );
    }

    else if (speed <= 66)
    {
        set_fan_speed(
            FAN_MEDIUM
        );
    }

    else
    {
        set_fan_speed(
            FAN_HIGH
        );
    }

    oled_show_status();

    char response[256];

    snprintf(
        response,
        sizeof(response),

        "{"
        "\"success\":true,"
        "\"speed\":%d,"
        "\"fanLevel\":\"%s\","
        "\"servoAngle\":%d"
        "}",

        speed,

        fan_text(
            current_fan_speed
        ),

        (
            current_fan_speed ==
            FAN_OFF
        )
            ? SERVO_OFF_DEG
            :
        (
            current_fan_speed ==
            FAN_LOW
        )
            ? SERVO_LOW_DEG
            :
        (
            current_fan_speed ==
            FAN_MEDIUM
        )
            ? SERVO_MEDIUM_DEG
            :
            SERVO_HIGH_DEG
    );

    return send_json_response(
        req,
        response
    );
}

static esp_err_t api_power_handler(
    httpd_req_t *req
)
{
    char body[128];

    if (
        req->content_len <= 0 ||
        req->content_len >=
            sizeof(body)
    )
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received = 0;

    int remaining =
        req->content_len;

    while (remaining > 0)
    {
        int ret =
            httpd_req_recv(
                req,
                body + received,
                remaining
            );

        if (ret <= 0)
        {
            httpd_resp_send_err(
                req,
                HTTPD_400_BAD_REQUEST,
                "Failed to read body"
            );

            return ESP_FAIL;
        }

        received += ret;

        remaining -= ret;
    }

    body[received] =
        '\0';

    char *power_ptr =
        strstr(
            body,
            "\"power\""
        );

    if (!power_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing power"
        );

        return ESP_FAIL;
    }

    power_ptr =
        strchr(
            power_ptr,
            ':'
        );

    if (!power_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid power"
        );

        return ESP_FAIL;
    }

    bool power =
        strstr(
            power_ptr + 1,
            "true"
        ) != NULL;

    set_power(
        power
    );

    char response[256];

    snprintf(
        response,
        sizeof(response),

        "{"
        "\"success\":true,"
        "\"power\":%s,"
        "\"fanLevel\":\"%s\","
        "\"servoAngle\":%d"
        "}",

        power_state
            ? "true"
            : "false",

        fan_text(
            current_fan_speed
        ),

        (
            current_fan_speed ==
            FAN_OFF
        )
            ? SERVO_OFF_DEG
            :
        (
            current_fan_speed ==
            FAN_LOW
        )
            ? SERVO_LOW_DEG
            :
        (
            current_fan_speed ==
            FAN_MEDIUM
        )
            ? SERVO_MEDIUM_DEG
            :
            SERVO_HIGH_DEG
    );

    return send_json_response(
        req,
        response
    );
}

static esp_err_t api_mode_handler(
    httpd_req_t *req
)
{
    char body[128];

    if (
        req->content_len <= 0 ||
        req->content_len >=
            sizeof(body)
    )
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received = 0;

    int remaining =
        req->content_len;

    while (remaining > 0)
    {
        int ret =
            httpd_req_recv(
                req,
                body + received,
                remaining
            );

        if (ret <= 0)
        {
            httpd_resp_send_err(
                req,
                HTTPD_400_BAD_REQUEST,
                "Failed to read body"
            );

            return ESP_FAIL;
        }

        received += ret;

        remaining -= ret;
    }

    body[received] =
        '\0';

    if (
        strstr(
            body,
            "\"mode\":\"auto\""
        )
    )
    {
        cancel_timer();

        start_auto_measurement();
    }

    else
    {
        cancel_auto_mode();

        display_mode =
            DISPLAY_NORMAL;
    }

    oled_show_status();

    return send_json_response(
        req,
        "{\"success\":true}"
    );
}

static esp_err_t api_sleep_handler(
    httpd_req_t *req
)
{
    char body[128];

    if (
        req->content_len <= 0 ||
        req->content_len >=
            sizeof(body)
    )
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received = 0;

    int remaining =
        req->content_len;

    while (remaining > 0)
    {
        int ret =
            httpd_req_recv(
                req,
                body + received,
                remaining
            );

        if (ret <= 0)
        {
            httpd_resp_send_err(
                req,
                HTTPD_400_BAD_REQUEST,
                "Failed to read body"
            );

            return ESP_FAIL;
        }

        received += ret;

        remaining -= ret;
    }

    body[received] =
        '\0';

    bool enabled =
        strstr(
            body,
            "true"
        ) != NULL;

    set_sleep_mode(
        enabled
    );

    return send_json_response(
        req,
        "{\"success\":true}"
    );
}

static esp_err_t api_timer_handler(
    httpd_req_t *req
)
{
    char body[128];

    if (
        req->content_len <= 0 ||
        req->content_len >=
            sizeof(body)
    )
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received = 0;

    int remaining =
        req->content_len;

    while (remaining > 0)
    {
        int ret =
            httpd_req_recv(
                req,
                body + received,
                remaining
            );

        if (ret <= 0)
        {
            httpd_resp_send_err(
                req,
                HTTPD_400_BAD_REQUEST,
                "Failed to read body"
            );

            return ESP_FAIL;
        }

        received += ret;

        remaining -= ret;
    }

    body[received] =
        '\0';

    char *minutes_ptr =
        strstr(
            body,
            "\"minutes\""
        );

    if (!minutes_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing minutes"
        );

        return ESP_FAIL;
    }

    minutes_ptr =
        strchr(
            minutes_ptr,
            ':'
        );

    if (!minutes_ptr)
    {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid minutes"
        );

        return ESP_FAIL;
    }

    int minutes = 0;

    sscanf(
        minutes_ptr + 1,
        "%d",
        &minutes
    );

    if (minutes == 30 ||
        minutes == 60)
    {
        set_timer_minutes(
            minutes
        );
    }

    else
    {
        cancel_timer();

        display_mode =
            DISPLAY_NORMAL;

        oled_show_status();
    }

    return send_json_response(
        req,
        "{\"success\":true}"
    );
}

static esp_err_t api_test_handler(
    httpd_req_t *req
)
{
    const char *uri =
        req->uri;

    cancel_auto_mode();

    cancel_timer();

    display_mode =
        DISPLAY_NORMAL;

    if (
        strstr(
            uri,
            "/low"
        )
    )
    {
        set_fan_speed(
            FAN_LOW
        );
    }

    else if (
        strstr(
            uri,
            "/medium"
        )
    )
    {
        set_fan_speed(
            FAN_MEDIUM
        );
    }

    else if (
        strstr(
            uri,
            "/high"
        )
    )
    {
        set_fan_speed(
            FAN_HIGH
        );
    }

    else if (
        strstr(
            uri,
            "/off"
        )
    )
    {
        set_fan_speed(
            FAN_OFF
        );
    }

    oled_show_status();

    return api_status_handler(
        req
    );
}

static void start_web_server(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port =
        HTTP_SERVER_PORT;

    config.max_uri_handlers =
        16;

    if (
        httpd_start(
            &web_server,
            &config
        ) != ESP_OK
    )
    {
        ESP_LOGE(
            TAG,
            "HTTP server failed"
        );

        return;
    }

    httpd_uri_t status_uri =
    {
        .uri =
            "/api/status",

        .method =
            HTTP_GET,

        .handler =
            api_status_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &status_uri
    );

    httpd_uri_t fan_uri =
    {
        .uri =
            "/api/fan",

        .method =
            HTTP_POST,

        .handler =
            api_fan_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &fan_uri
    );

    httpd_uri_t power_uri =
    {
        .uri =
            "/api/power",

        .method =
            HTTP_POST,

        .handler =
            api_power_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &power_uri
    );

    httpd_uri_t mode_uri =
    {
        .uri =
            "/api/mode",

        .method =
            HTTP_POST,

        .handler =
            api_mode_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &mode_uri
    );

    httpd_uri_t sleep_uri =
    {
        .uri =
            "/api/sleep",

        .method =
            HTTP_POST,

        .handler =
            api_sleep_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &sleep_uri
    );

    httpd_uri_t timer_uri =
    {
        .uri =
            "/api/timer",

        .method =
            HTTP_POST,

        .handler =
            api_timer_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &timer_uri
    );

    httpd_uri_t low_uri =
    {
        .uri =
            "/api/test/low",

        .method =
            HTTP_GET,

        .handler =
            api_test_handler,

        .user_ctx =
            NULL
    };

    httpd_uri_t medium_uri =
    {
        .uri =
            "/api/test/medium",

        .method =
            HTTP_GET,

        .handler =
            api_test_handler,

        .user_ctx =
            NULL
    };

    httpd_uri_t high_uri =
    {
        .uri =
            "/api/test/high",

        .method =
            HTTP_GET,

        .handler =
            api_test_handler,

        .user_ctx =
            NULL
    };

    httpd_uri_t off_uri =
    {
        .uri =
            "/api/test/off",

        .method =
            HTTP_GET,

        .handler =
            api_test_handler,

        .user_ctx =
            NULL
    };

    httpd_register_uri_handler(
        web_server,
        &low_uri
    );

    httpd_register_uri_handler(
        web_server,
        &medium_uri
    );

    httpd_register_uri_handler(
        web_server,
        &high_uri
    );

    httpd_register_uri_handler(
        web_server,
        &off_uri
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "HTTP SERVER STARTED"
    );

    ESP_LOGI(
        TAG,
        "GET  /api/status"
    );

    ESP_LOGI(
        TAG,
        "POST /api/fan"
    );

    ESP_LOGI(
        TAG,
        "POST /api/power"
    );

    ESP_LOGI(
        TAG,
        "POST /api/mode"
    );

    ESP_LOGI(
        TAG,
        "POST /api/sleep"
    );

    ESP_LOGI(
        TAG,
        "POST /api/timer"
    );

    ESP_LOGI(
        TAG,
        "GET  /api/test/off"
    );

    ESP_LOGI(
        TAG,
        "GET  /api/test/low"
    );

    ESP_LOGI(
        TAG,
        "GET  /api/test/medium"
    );

    ESP_LOGI(
        TAG,
        "GET  /api/test/high"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );
}

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "       ECRAFTONIC AIR-1"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );

    esp_err_t ret =
        nvs_flash_init();

    if (
        ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret =
            nvs_flash_init();
    }

    ESP_ERROR_CHECK(
        ret
    );

    buzzer_init();

    touch_init();

    servo_init();

    dust_sensor_init();

    oled_init();

    oled_clear();

    set_fan_speed(
        FAN_OFF
    );

    update_dust_reading();

    oled_show_status();

    wifi_init();

    start_web_server();

    xTaskCreate(
        button_task,
        "button_task",
        4096,
        NULL,
        5,
        NULL
    );

    xTaskCreate(
        purifier_task,
        "purifier_task",
        8192,
        NULL,
        5,
        NULL
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "Ecraftonic Air-1 READY"
    );

    ESP_LOGI(
        TAG,
        "STATIC IP: 10.16.90.50"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );
}