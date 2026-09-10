#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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


/* ============================================================
 *                       GPIO CONNECTIONS
 * ============================================================ */

/* OLED */
#define OLED_SDA_GPIO           GPIO_NUM_4
#define OLED_SCL_GPIO           GPIO_NUM_5

/* Touch sensors */
#define SPEED_TOUCH_GPIO        GPIO_NUM_12
#define AUTO_TOUCH_GPIO         GPIO_NUM_15
#define TIMER_TOUCH_GPIO        GPIO_NUM_23

/* Servo */
#define SERVO_GPIO              GPIO_NUM_18

/* Buzzer */
#define BUZZER_GPIO             GPIO_NUM_19

/* GP2Y1010AU0F */
#define DUST_LED_GPIO           GPIO_NUM_25

/* GPIO34 = ADC1_CHANNEL_6 */
#define DUST_ADC_CHANNEL        ADC_CHANNEL_6


/* ============================================================
 *                       OLED
 * ============================================================ */

#define OLED_I2C_ADDRESS        0x3C

#define OLED_WIDTH              128
#define OLED_HEIGHT             64

#define OLED_I2C_SPEED          400000


/* ============================================================
 *                       SERVO
 * ============================================================ */

#define SERVO_FREQUENCY         50

#define SERVO_OFF_DEG           0
#define SERVO_LOW_DEG           95
#define SERVO_MEDIUM_DEG        120
#define SERVO_HIGH_DEG          140

#define SERVO_MIN_PULSE_US      500
#define SERVO_MAX_PULSE_US      2500


/* ============================================================
 *                       DUST SENSOR
 * ============================================================ */

#define AUTO_SAMPLES            60

#define DUST_SAMPLE_INTERVAL_MS 1000

/*
 * GP2Y1010AU0F LED timing.
 *
 * LED ON
 * wait approximately 280 us
 * ADC read around 320 us
 * LED OFF
 */
#define DUST_LED_ON_US          280
#define DUST_ADC_WAIT_US        40


/* ============================================================
 *                       AIR QUALITY LIMITS
 * ============================================================ */

#define DUST_LOW_LIMIT          35.0f
#define DUST_MEDIUM_LIMIT       75.0f
#define DUST_HIGH_LIMIT         500.0f


/* ============================================================
 *                       TIMER
 * ============================================================ */

#define TIMER_30_MIN_MS         (30ULL * 60ULL * 1000ULL)
#define TIMER_60_MIN_MS         (60ULL * 60ULL * 1000ULL)

#define AUTO_DURATION_MS        (60ULL * 60ULL * 1000ULL)


/* ============================================================
 *                       BUZZER
 * ============================================================ */

#define BUZZER_BEEP_MS          100


/* ============================================================
 *                       TAG
 * ============================================================ */

static const char *TAG = "ECOAIR";


/* ============================================================
 *                       ENUMS
 * ============================================================ */

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


/* ============================================================
 *                       GLOBAL STATE
 * ============================================================ */

/* OLED */
static i2c_master_bus_handle_t oled_bus = NULL;
static i2c_master_dev_handle_t oled_dev = NULL;

static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];


/* ADC */
static adc_oneshot_unit_handle_t adc_handle = NULL;


/* Current system state */
static fan_speed_t current_fan_speed = FAN_OFF;

static aqi_level_t current_aqi = AQI_LOW;

static float current_dust = 0.0f;


/* Timer */
static bool timer_active = false;

static int timer_minutes = 0;

static uint64_t timer_end_time = 0;


/* Auto */
static bool auto_mode = false;

static bool auto_measuring = false;

static uint64_t auto_end_time = 0;

static int auto_sample_count = 0;

static float auto_dust_sum = 0.0f;

static uint64_t next_auto_sample_time = 0;


/* ============================================================
 *                       FONT
 *
 * 5 x 7 font
 * ============================================================ */

static const uint8_t font5x7[][5] =
{
    /* SPACE */
    {0x00,0x00,0x00,0x00,0x00},

    /* A */
    {0x7E,0x11,0x11,0x11,0x7E},

    /* B */
    {0x7F,0x49,0x49,0x49,0x36},

    /* C */
    {0x3E,0x41,0x41,0x41,0x22},

    /* D */
    {0x7F,0x41,0x41,0x22,0x1C},

    /* E */
    {0x7F,0x49,0x49,0x49,0x41},

    /* F */
    {0x7F,0x09,0x09,0x09,0x01},

    /* G */
    {0x3E,0x41,0x49,0x49,0x7A},

    /* H */
    {0x7F,0x08,0x08,0x08,0x7F},

    /* I */
    {0x00,0x41,0x7F,0x41,0x00},

    /* J */
    {0x20,0x40,0x41,0x3F,0x01},

    /* K */
    {0x7F,0x08,0x14,0x22,0x41},

    /* L */
    {0x7F,0x40,0x40,0x40,0x40},

    /* M */
    {0x7F,0x02,0x0C,0x02,0x7F},

    /* N */
    {0x7F,0x04,0x08,0x10,0x7F},

    /* O */
    {0x3E,0x41,0x41,0x41,0x3E},

    /* P */
    {0x7F,0x09,0x09,0x09,0x06},

    /* Q */
    {0x3E,0x41,0x51,0x21,0x5E},

    /* R */
    {0x7F,0x09,0x19,0x29,0x46},

    /* S */
    {0x46,0x49,0x49,0x49,0x31},

    /* T */
    {0x01,0x01,0x7F,0x01,0x01},

    /* U */
    {0x3F,0x40,0x40,0x40,0x3F},

    /* V */
    {0x1F,0x20,0x40,0x20,0x1F},

    /* W */
    {0x7F,0x20,0x18,0x20,0x7F},

    /* X */
    {0x63,0x14,0x08,0x14,0x63},

    /* Y */
    {0x07,0x08,0x70,0x08,0x07},

    /* Z */
    {0x61,0x51,0x49,0x45,0x43},

    /* 0 */
    {0x3E,0x45,0x49,0x51,0x3E},

    /* 1 */
    {0x00,0x21,0x7F,0x01,0x00},

    /* 2 */
    {0x21,0x43,0x45,0x49,0x31},

    /* 3 */
    {0x42,0x41,0x51,0x69,0x46},

    /* 4 */
    {0x0C,0x14,0x24,0x7F,0x04},

    /* 5 */
    {0x72,0x51,0x51,0x51,0x4E},

    /* 6 */
    {0x1E,0x29,0x49,0x49,0x06},

    /* 7 */
    {0x40,0x47,0x48,0x50,0x60},

    /* 8 */
    {0x36,0x49,0x49,0x49,0x36},

    /* 9 */
    {0x30,0x49,0x49,0x4A,0x3C},

    /* : */
    {0x00,0x36,0x36,0x00,0x00},

    /* - */
    {0x08,0x08,0x08,0x08,0x08}
};


/* ============================================================
 *                       FONT LOOKUP
 * ============================================================ */

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

    return 0;
}


/* ============================================================
 *                       OLED PIXEL
 * ============================================================ */

static void oled_pixel(
    int x,
    int y,
    bool on
)
{
    if (x < 0 || x >= OLED_WIDTH)
        return;

    if (y < 0 || y >= OLED_HEIGHT)
        return;

    int index =
        x + (y / 8) * OLED_WIDTH;

    uint8_t mask =
        1 << (y % 8);

    if (on)
        oled_buffer[index] |= mask;
    else
        oled_buffer[index] &= ~mask;
}


/* ============================================================
 *                       OLED CHARACTER
 * ============================================================ */

static void oled_draw_char(
    int x,
    int y,
    char c,
    int scale
)
{
    int index = font_index(c);

    for (int col = 0; col < 5; col++)
    {
        uint8_t column =
            font5x7[index][col];

        for (int row = 0; row < 7; row++)
        {
            if (column & (1 << row))
            {
                for (int dx = 0; dx < scale; dx++)
                {
                    for (int dy = 0; dy < scale; dy++)
                    {
                        oled_pixel(
                            x + col * scale + dx,
                            y + row * scale + dy,
                            true
                        );
                    }
                }
            }
        }
    }
}


/* ============================================================
 *                       OLED STRING
 * ============================================================ */

static void oled_draw_string(
    int x,
    int y,
    const char *text,
    int scale
)
{
    while (*text)
    {
        oled_draw_char(
            x,
            y,
            *text,
            scale
        );

        x += 6 * scale;

        text++;
    }
}


/* ============================================================
 *                       OLED COMMAND
 * ============================================================ */

static void oled_command(uint8_t command)
{
    uint8_t data[2] =
    {
        0x00,
        command
    };

    ESP_ERROR_CHECK(
        i2c_master_transmit(
            oled_dev,
            data,
            sizeof(data),
            100
        )
    );
}


/* ============================================================
 *                       OLED DATA
 * ============================================================ */

static void oled_send_data(
    const uint8_t *data,
    size_t length
)
{
    /*
     * SSD1306 requires control byte 0x40
     * before display data.
     */

    uint8_t packet[129];

    packet[0] = 0x40;

    memcpy(
        &packet[1],
        data,
        length
    );

    ESP_ERROR_CHECK(
        i2c_master_transmit(
            oled_dev,
            packet,
            length + 1,
            100
        )
    );
}


/* ============================================================
 *                       OLED INIT
 * ============================================================ */

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
            &oled_bus
        )
    );


    i2c_device_config_t device_config =
    {
        .dev_addr_length =
            I2C_ADDR_BIT_LEN_7,

        .device_address =
            OLED_I2C_ADDRESS,

        .scl_speed_hz =
            OLED_I2C_SPEED
    };


    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(
            oled_bus,
            &device_config,
            &oled_dev
        )
    );


    /*
     * SSD1306 initialization
     */

    oled_command(0xAE);

    oled_command(0x20);
    oled_command(0x00);

    oled_command(0xB0);

    oled_command(0xC8);

    oled_command(0x00);
    oled_command(0x10);

    oled_command(0x40);

    oled_command(0x81);
    oled_command(0x7F);

    oled_command(0xA1);

    oled_command(0xA6);

    oled_command(0xA8);
    oled_command(0x3F);

    oled_command(0xA4);

    oled_command(0xD3);
    oled_command(0x00);

    oled_command(0xD5);
    oled_command(0x80);

    oled_command(0xD9);
    oled_command(0xF1);

    oled_command(0xDA);
    oled_command(0x12);

    oled_command(0xDB);
    oled_command(0x40);

    oled_command(0x8D);
    oled_command(0x14);

    oled_command(0xAF);
}


/* ============================================================
 *                       OLED CLEAR
 * ============================================================ */

static void oled_clear(void)
{
    memset(
        oled_buffer,
        0,
        sizeof(oled_buffer)
    );
}


/* ============================================================
 *                       OLED UPDATE
 * ============================================================ */

static void oled_update(void)
{
    for (int page = 0; page < 8; page++)
    {
        oled_command(
            0xB0 + page
        );

        oled_command(0x00);
        oled_command(0x10);

        oled_send_data(
            &oled_buffer[page * 128],
            128
        );
    }
}


/* ============================================================
 *                       SERVO INIT
 * ============================================================ */

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

        .duty = 0,

        .hpoint = 0
    };


    ESP_ERROR_CHECK(
        ledc_channel_config(
            &channel_config
        )
    );
}


/* ============================================================
 *                       SERVO ANGLE
 * ============================================================ */

static void servo_set_angle(int angle)
{
    if (angle < 0)
        angle = 0;

    if (angle > 180)
        angle = 180;


    /*
     * MG90S servo:
     *
     * Frequency = 50 Hz
     * Period    = 20 ms
     *
     * Pulse:
     * 500 us  -> 0 degree
     * 2500 us -> 180 degree
     */

    uint32_t pulse_us =
        SERVO_MIN_PULSE_US +
        (
            (uint32_t)(SERVO_MAX_PULSE_US -
                       SERVO_MIN_PULSE_US)
            * (uint32_t)angle
        ) / 180;


    /*
     * 50 Hz = 20,000 us period
     *
     * LEDC resolution = 16 bit
     *
     * Duty = pulse / period × 65535
     */

    uint32_t duty =
        ((uint64_t)pulse_us * 65535ULL)
        / 20000ULL;


    /*
     * IMPORTANT:
     *
     * Use normal LEDC duty update.
     * Do NOT use ledc_set_duty_and_update()
     * because that invokes the fade mechanism
     * in this ESP-IDF version.
     */

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
        "Servo angle = %d deg, pulse = %lu us",
        angle,
        (unsigned long)pulse_us
    );
}


/* ============================================================
 *                       FAN CONTROL
 * ============================================================ */

static void set_fan_speed(
    fan_speed_t speed
)
{
    current_fan_speed = speed;


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
        "Fan speed = %d",
        speed
    );
}


/* ============================================================
 *                       BUZZER
 * ============================================================ */

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
        gpio_config(&config)
    );


    gpio_set_level(
        BUZZER_GPIO,
        0
    );
}


/* ============================================================
 *                       BUZZER BEEP
 * ============================================================ */

static void buzzer_beep(void)
{
    gpio_set_level(
        BUZZER_GPIO,
        1
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            BUZZER_BEEP_MS
        )
    );

    gpio_set_level(
        BUZZER_GPIO,
        0
    );
}


/* ============================================================
 *                       DUST SENSOR INIT
 * ============================================================ */

static void dust_sensor_init(void)
{
    /*
     * GPIO25 controls the IR LED pulse.
     */

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


    /*
     * New ESP-IDF ADC oneshot driver.
     */

    adc_oneshot_unit_init_cfg_t adc_config =
    {
        .unit_id = ADC_UNIT_1,

        .ulp_mode =
            ADC_ULP_MODE_DISABLE
    };


    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(
            &adc_config,
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
}


/* ============================================================
 *                       RAW DUST ADC
 * ============================================================ */

static int read_dust_raw(void)
{
    /*
     * Turn IR LED ON.
     */

    gpio_set_level(
        DUST_LED_GPIO,
        1
    );


    /*
     * GP2Y1010AU0F sampling timing.
     */

    esp_rom_delay_us(
        DUST_LED_ON_US
    );

    esp_rom_delay_us(
        DUST_ADC_WAIT_US
    );


    int raw = 0;


    ESP_ERROR_CHECK(
        adc_oneshot_read(
            adc_handle,
            DUST_ADC_CHANNEL,
            &raw
        )
    );


    /*
     * Turn LED OFF.
     */

    gpio_set_level(
        DUST_LED_GPIO,
        0
    );


    return raw;
}


/* ============================================================
 *                       ADC → VOLTAGE
 * ============================================================ */

static float adc_raw_to_voltage(
    int raw
)
{
    /*
     * Initial raw ADC approximation.
     *
     * Later we can add ESP-IDF ADC calibration.
     */

    return
        ((float)raw / 4095.0f)
        * 3.3f;
}


/* ============================================================
 *                       DUST VOLTAGE
 * ============================================================ */

static float read_dust_voltage(void)
{
    int raw =
        read_dust_raw();


    float adc_voltage =
        adc_raw_to_voltage(
            raw
        );


    /*
     * Your voltage divider:
     *
     *
     * Sensor output
     *       |
     *      10k
     *       |
     *       +-------- GPIO34
     *       |
     *      20k
     *       |
     *      GND
     *
     *
     * GPIO voltage =
     *
     * Sensor voltage × 20/(10+20)
     *
     * Therefore:
     *
     * Sensor voltage =
     * GPIO voltage × 1.5
     */

    float sensor_voltage =
        adc_voltage * 1.5f;


    return sensor_voltage;
}


/* ============================================================
 *                       VOLTAGE → DUST
 * ============================================================ */

static float voltage_to_dust(
    float voltage
)
{
    /*
     * IMPORTANT:
     *
     * This is an INITIAL approximation.
     *
     * GP2Y1010AU0F requires calibration for accurate
     * µg/m³ measurements.
     */


    const float baseline =
        0.60f;


    const float sensitivity =
        0.50f;


    float dust =
        (voltage - baseline)
        / sensitivity;


    /*
     * Convert mg/m³ → µg/m³.
     */

    dust *= 1000.0f;


    if (dust < 0.0f)
        dust = 0.0f;


    if (dust > 1000.0f)
        dust = 1000.0f;


    return dust;
}


/* ============================================================
 *                       READ DUST
 * ============================================================ */

static float read_dust(void)
{
    float voltage =
        read_dust_voltage();


    float dust =
        voltage_to_dust(
            voltage
        );


    return dust;
}


/* ============================================================
 *                       AQI CATEGORY
 * ============================================================ */

static aqi_level_t get_aqi(
    float dust
)
{
    if (dust <= DUST_LOW_LIMIT)
        return AQI_LOW;


    if (dust <= DUST_MEDIUM_LIMIT)
        return AQI_MEDIUM;


    return AQI_HIGH;
}


/* ============================================================
 *                       TEXT HELPERS
 * ============================================================ */

static const char *aqi_text(
    aqi_level_t aqi
)
{
    switch (aqi)
    {
        case AQI_LOW:
            return "LOW";

        case AQI_MEDIUM:
            return "MEDIUM";

        case AQI_HIGH:
            return "HIGH";
    }

    return "LOW";
}


static const char *fan_text(
    fan_speed_t speed
)
{
    switch (speed)
    {
        case FAN_OFF:
            return "OFF";

        case FAN_LOW:
            return "LOW";

        case FAN_MEDIUM:
            return "MEDIUM";

        case FAN_HIGH:
            return "HIGH";
    }

    return "OFF";
}


/* ============================================================
 *                       OLED SCREEN
 * ============================================================ */

static void update_oled(void)
{
    char line[32];


    oled_clear();


    /*
     * LINE 1
     *
     * Large font for visibility.
     */

    snprintf(
        line,
        sizeof(line),
        "AQI : %s",
        aqi_text(
            current_aqi
        )
    );


    oled_draw_string(
        0,
        0,
        line,
        2
    );


    /*
     * AUTO MODE
     */

    if (auto_mode)
    {
        snprintf(
            line,
            sizeof(line),
            "Fan Speed : %s",
            fan_text(
                current_fan_speed
            )
        );


        oled_draw_string(
            0,
            20,
            line,
            1
        );


        oled_draw_string(
            0,
            46,
            "AUTO MODE ON",
            1
        );
    }


    /*
     * TIMER MODE
     */

    else if (timer_active)
    {
        snprintf(
            line,
            sizeof(line),
            "TIMER : %d mins",
            timer_minutes
        );


        oled_draw_string(
            0,
            27,
            line,
            1
        );
    }


    /*
     * NORMAL MODE
     */

    else
    {
        snprintf(
            line,
            sizeof(line),
            "Fan Speed : %s",
            fan_text(
                current_fan_speed
            )
        );


        oled_draw_string(
            0,
            27,
            line,
            1
        );
    }


    oled_update();
}


/* ============================================================
 *                       TOUCH INIT
 * ============================================================ */

static void touch_init(void)
{
    gpio_config_t config =
    {
        .pin_bit_mask =
            (1ULL << SPEED_TOUCH_GPIO) |
            (1ULL << AUTO_TOUCH_GPIO) |
            (1ULL << TIMER_TOUCH_GPIO),

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
        gpio_config(&config)
    );
}


/* ============================================================
 *                       TOUCH DEBOUNCE
 * ============================================================ */

static bool touch_pressed(
    gpio_num_t gpio,
    int *last_state,
    uint64_t *last_press_time
)
{
    int state =
        gpio_get_level(gpio);


    uint64_t now =
        esp_timer_get_time()
        / 1000ULL;


    bool pressed = false;


    /*
     * Most TTP223-style touch modules:
     *
     * untouched = LOW
     * touched   = HIGH
     *
     * If your module behaves opposite,
     * change this condition.
     */

    if (state == 1 &&
        *last_state == 0)
    {
        /*
         * 250 ms debounce.
         */

        if (now - *last_press_time >= 250)
        {
            pressed = true;

            *last_press_time = now;
        }
    }


    *last_state = state;


    return pressed;
}


/* ============================================================
 *                       START AUTO
 * ============================================================ */

static void start_auto_mode(void)
{
    ESP_LOGI(
        TAG,
        "AUTO MODE STARTED"
    );


    /*
     * Start a completely new AUTO cycle.
     *
     * This means pressing AUTO again also
     * restarts the process.
     */

    auto_mode = true;

    auto_measuring = true;


    auto_sample_count = 0;

    auto_dust_sum = 0.0f;


    /*
     * Keep current fan speed.
     *
     * DO NOT change servo here.
     */


    next_auto_sample_time =
        esp_timer_get_time()
        / 1000ULL;


    /*
     * Timer mode is cancelled when AUTO
     * is selected.
     */

    timer_active = false;

    timer_minutes = 0;


    buzzer_beep();

    update_oled();
}


/* ============================================================
 *                       AUTO SAMPLE
 * ============================================================ */

static void auto_take_sample(void)
{
    uint64_t now =
        esp_timer_get_time()
        / 1000ULL;


    if (now < next_auto_sample_time)
        return;


    /*
     * Read one dust value.
     */

    float dust =
        read_dust();


    current_dust =
        dust;


    current_aqi =
        get_aqi(
            dust
        );


    auto_dust_sum +=
        dust;


    auto_sample_count++;


    ESP_LOGI(
        TAG,
        "AUTO sample %d/%d = %.2f ug/m3",
        auto_sample_count,
        AUTO_SAMPLES,
        dust
    );


    /*
     * Display current AQI while measuring.
     */

    update_oled();


    /*
     * Measurement complete.
     */

    if (auto_sample_count >= AUTO_SAMPLES)
    {
        float average =
            auto_dust_sum /
            (float)AUTO_SAMPLES;


        current_dust =
            average;


        current_aqi =
            get_aqi(
                average
            );


        ESP_LOGI(
            TAG,
            "AUTO average = %.2f ug/m3",
            average
        );


        /*
         * Determine fan speed.
         */

        if (average <= DUST_LOW_LIMIT)
        {
            set_fan_speed(
                FAN_LOW
            );
        }

        else if (average <= DUST_MEDIUM_LIMIT)
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


        /*
         * Measurement finished.
         */

        auto_measuring = false;


        /*
         * AUTO runs for 60 minutes.
         */

        auto_end_time =
            now +
            AUTO_DURATION_MS;


        update_oled();

        return;
    }


    /*
     * Next reading in one second.
     */

    next_auto_sample_time =
        now +
        DUST_SAMPLE_INTERVAL_MS;
}


/* ============================================================
 *                       SPEED BUTTON
 * ============================================================ */

static void handle_speed_button(void)
{
    ESP_LOGI(
        TAG,
        "SPEED BUTTON"
    );


    /*
     * OFF → LOW → MEDIUM → HIGH → OFF
     */

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


    /*
     * Manual speed selection exits AUTO.
     */

    auto_mode = false;

    auto_measuring = false;


    /*
     * Manual speed selection also cancels timer.
     */

    timer_active = false;

    timer_minutes = 0;


    buzzer_beep();

    update_oled();
}


/* ============================================================
 *                       TIMER BUTTON
 * ============================================================ */

static void handle_timer_button(void)
{
    uint64_t now =
        esp_timer_get_time()
        / 1000ULL;


    ESP_LOGI(
        TAG,
        "TIMER BUTTON"
    );


    /*
     * OFF → 30 → 60 → OFF
     */

    if (!timer_active)
    {
        timer_active = true;

        timer_minutes = 30;

        timer_end_time =
            now +
            TIMER_30_MIN_MS;
    }

    else if (timer_minutes == 30)
    {
        timer_minutes = 60;

        timer_end_time =
            now +
            TIMER_60_MIN_MS;
    }

    else
    {
        timer_active = false;

        timer_minutes = 0;

        timer_end_time = 0;
    }


    /*
     * Timer selection exits AUTO.
     */

    auto_mode = false;

    auto_measuring = false;


    buzzer_beep();

    update_oled();
}


/* ============================================================
 *                       CHECK TIMER
 * ============================================================ */

static void check_timer(void)
{
    if (!timer_active)
        return;


    uint64_t now =
        esp_timer_get_time()
        / 1000ULL;


    if (now >= timer_end_time)
    {
        ESP_LOGI(
            TAG,
            "TIMER FINISHED"
        );


        timer_active = false;

        timer_minutes = 0;

        timer_end_time = 0;


        /*
         * Timer finished:
         *
         * servo → 0°
         * fan → OFF
         */

        set_fan_speed(
            FAN_OFF
        );


        update_oled();
    }
}


/* ============================================================
 *                       CHECK AUTO
 * ============================================================ */

static void check_auto(void)
{
    if (!auto_mode)
        return;


    /*
     * During the 1-minute measurement,
     * don't check the 60-minute timer.
     */

    if (auto_measuring)
        return;


    uint64_t now =
        esp_timer_get_time()
        / 1000ULL;


    if (now >= auto_end_time)
    {
        ESP_LOGI(
            TAG,
            "AUTO 60 MIN FINISHED"
        );


        /*
         * AUTO finished:
         *
         * fan → MEDIUM
         */

        auto_mode = false;

        auto_end_time = 0;


        set_fan_speed(
            FAN_MEDIUM
        );


        update_oled();
    }
}


/* ============================================================
 *                       MAIN TASK
 * ============================================================ */

static void air_purifier_task(
    void *arg
)
{
    int last_speed_state = 0;

    int last_auto_state = 0;

    int last_timer_state = 0;


    uint64_t last_dust_read = 0;


    uint64_t speed_last_press = 0;

    uint64_t auto_last_press = 0;

    uint64_t timer_last_press = 0;


    while (1)
    {
        uint64_t now =
            esp_timer_get_time()
            / 1000ULL;


        /* ====================================================
         * TOUCH SENSOR 1
         * SPEED
         * ==================================================== */

        if (touch_pressed(
                SPEED_TOUCH_GPIO,
                &last_speed_state,
                &speed_last_press))
        {
            handle_speed_button();
        }


        /* ====================================================
         * TOUCH SENSOR 2
         * AUTO
         * ==================================================== */

        if (touch_pressed(
                AUTO_TOUCH_GPIO,
                &last_auto_state,
                &auto_last_press))
        {
            /*
             * Pressing AUTO at ANY time starts
             * a completely new 1-minute measurement.
             */

            start_auto_mode();
        }


        /* ====================================================
         * TOUCH SENSOR 3
         * TIMER
         * ==================================================== */

        if (touch_pressed(
                TIMER_TOUCH_GPIO,
                &last_timer_state,
                &timer_last_press))
        {
            handle_timer_button();
        }


        /* ====================================================
         * AUTO MEASUREMENT
         * ==================================================== */

        if (auto_measuring)
        {
            auto_take_sample();
        }


        /* ====================================================
         * AUTO 60 MIN TIMER
         * ==================================================== */

        check_auto();


        /* ====================================================
         * NORMAL TIMER
         * ==================================================== */

        check_timer();


        /* ====================================================
         * CONTINUOUS AQI READING
         *
         * Read once every second when AUTO
         * measurement isn't already taking samples.
         * ==================================================== */

        if (!auto_measuring &&
            now - last_dust_read >= 1000)
        {
            last_dust_read = now;


            current_dust =
                read_dust();


            current_aqi =
                get_aqi(
                    current_dust
                );


            ESP_LOGI(
                TAG,
                "Dust: %.2f ug/m3 | AQI: %s",
                current_dust,
                aqi_text(
                    current_aqi
                )
            );


            update_oled();
        }


        /*
         * Small task delay.
         */

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}


/* ============================================================
 *                       APP MAIN
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "================================="
    );

    ESP_LOGI(
        TAG,
        "       ECOAIR AIR PURIFIER"
    );

    ESP_LOGI(
        TAG,
        "================================="
    );


    /* --------------------------------------------------------
     * Initialize OLED
     * -------------------------------------------------------- */

    oled_init();


    /* --------------------------------------------------------
     * Initialize touch sensors
     * -------------------------------------------------------- */

    touch_init();


    /* --------------------------------------------------------
     * Initialize buzzer
     * -------------------------------------------------------- */

    buzzer_init();


    /* --------------------------------------------------------
     * Initialize servo
     * -------------------------------------------------------- */

    servo_init();


    /* --------------------------------------------------------
     * Initialize dust sensor / ADC
     * -------------------------------------------------------- */

    dust_sensor_init();


    /* --------------------------------------------------------
     * Initial fan state
     *
     * 0 degrees = OFF
     * -------------------------------------------------------- */

    set_fan_speed(
        FAN_OFF
    );


    /* --------------------------------------------------------
     * Initial AQI
     * -------------------------------------------------------- */

    current_dust = 0.0f;

    current_aqi = AQI_LOW;


    /* --------------------------------------------------------
     * Initial OLED
     * -------------------------------------------------------- */

    update_oled();


    ESP_LOGI(
        TAG,
        "System ready."
    );


    /* --------------------------------------------------------
     * Start main task
     * -------------------------------------------------------- */

    xTaskCreate(
        air_purifier_task,
        "air_purifier_task",
        8192,
        NULL,
        5,
        NULL
    );
}