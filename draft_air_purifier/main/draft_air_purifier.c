#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "esp_http_client.h"


/* =========================================================
                    USER CONFIGURATION
   ========================================================= */

/*
 * Wi-Fi
 *
 * Put YOUR Wi-Fi credentials here.
 */
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"


/*
 * Blynk
 *
 * Create a Blynk template/device and put the device token here.
 */
#define BLYNK_TOKEN     "YOUR_BLYNK_DEVICE_TOKEN"


/* =========================================================
                    PIN DEFINITIONS
   ========================================================= */

/* OLED */
#define OLED_SDA_GPIO       GPIO_NUM_4
#define OLED_SCL_GPIO       GPIO_NUM_5
#define OLED_I2C_ADDRESS    0x3C


/* Touch sensors */
#define SPEED_TOUCH_GPIO    GPIO_NUM_13
#define AUTO_TOUCH_GPIO     GPIO_NUM_14
#define TIMER_TOUCH_GPIO    GPIO_NUM_27

/*
 * TTP223-type touch modules normally output HIGH
 * when touched.
 *
 * If your modules output LOW when touched,
 * change this to 0.
 */
#define TOUCH_ACTIVE_LEVEL  1


/* Servo */
#define SERVO_GPIO          GPIO_NUM_18


/* Buzzer */
#define BUZZER_GPIO         GPIO_NUM_26


/* Dust sensor analog output */
#define DUST_ADC_CHANNEL    ADC_CHANNEL_6
/*
 * ADC_CHANNEL_6 on classic ESP32 = GPIO34
 */


/* =========================================================
                    SERVO ANGLES
   ========================================================= */

#define SERVO_OFF           0
#define SERVO_LOW           80
#define SERVO_MEDIUM        120
#define SERVO_HIGH          150


/* =========================================================
                    DUST SETTINGS
   ========================================================= */

#define DUST_SAMPLES        60

/*
 * Your voltage divider:
 *
 * Sensor OUT
 *      |
 *     10k
 *      |
 *     GPIO34
 *      |
 *     20k
 *      |
 *     GND
 *
 * Vgpio = Vsensor * 20/(10+20)
 *
 * Therefore:
 *
 * Vsensor = Vgpio * 1.5
 */


/*
 * Approximate zero-voltage value for GP2Y1010AU0F.
 *
 * This MUST eventually be calibrated with your actual
 * sensor.
 */
#define DUST_VZERO          0.90f


/*
 * Approximate sensor sensitivity:
 *
 * 0.5 V / 100 ug/m3
 *
 * = 200 ug/m3 per volt
 */
#define DUST_SENSITIVITY    200.0f


/* =========================================================
                    GLOBAL VARIABLES
   ========================================================= */

static const char *TAG = "AIR_PURIFIER";


typedef enum
{
    FAN_OFF = 0,
    FAN_LOW,
    FAN_MEDIUM,
    FAN_HIGH
} fan_speed_t;


typedef enum
{
    MODE_MANUAL = 0,
    MODE_AUTO
} purifier_mode_t;


typedef enum
{
    TIMER_OFF = 0,
    TIMER_30_MIN,
    TIMER_60_MIN
} timer_mode_t;


static fan_speed_t fan_speed = FAN_OFF;

static purifier_mode_t purifier_mode = MODE_MANUAL;

static timer_mode_t timer_mode = TIMER_OFF;


/* Latest dust data */
static float dust_average = 0.0f;

static int estimated_aqi = 0;


/* Timer */
static uint32_t timer_remaining_seconds = 0;


/*
 * OLED temporary message.
 *
 * AQI is ALWAYS displayed.
 */
static char oled_message_1[22] = "";

static char oled_message_2[22] = "";

static uint32_t oled_message_until = 0;


/* ADC */
static adc_oneshot_unit_handle_t adc_handle;

static adc_cali_handle_t adc_cali_handle = NULL;

static bool adc_calibrated = false;


/* OLED */
static i2c_master_bus_handle_t i2c_bus;

static i2c_master_dev_handle_t oled_handle;


/* Wi-Fi */
static bool wifi_connected = false;


/* =========================================================
                    OLED FONT
   ========================================================= */

/*
 * 5x7 ASCII font.
 *
 * Characters used by the purifier.
 */

static const uint8_t font5x7[][5] =
{
    /* SPACE */
    [32] = {0x00,0x00,0x00,0x00,0x00},

    /* - */
    [45] = {0x08,0x08,0x08,0x08,0x08},

    /* 0 */
    [48] = {0x3E,0x51,0x49,0x45,0x3E},

    /* 1 */
    [49] = {0x00,0x42,0x7F,0x40,0x00},

    /* 2 */
    [50] = {0x42,0x61,0x51,0x49,0x46},

    /* 3 */
    [51] = {0x21,0x41,0x45,0x4B,0x31},

    /* 4 */
    [52] = {0x18,0x14,0x12,0x7F,0x10},

    /* 5 */
    [53] = {0x27,0x45,0x45,0x45,0x39},

    /* 6 */
    [54] = {0x3C,0x4A,0x49,0x49,0x30},

    /* 7 */
    [55] = {0x01,0x71,0x09,0x05,0x03},

    /* 8 */
    [56] = {0x36,0x49,0x49,0x49,0x36},

    /* 9 */
    [57] = {0x06,0x49,0x49,0x29,0x1E},

    /* A */
    [65] = {0x7E,0x11,0x11,0x11,0x7E},

    /* B */
    [66] = {0x7F,0x49,0x49,0x49,0x36},

    /* C */
    [67] = {0x3E,0x41,0x41,0x41,0x22},

    /* D */
    [68] = {0x7F,0x41,0x41,0x22,0x1C},

    /* E */
    [69] = {0x7F,0x49,0x49,0x49,0x41},

    /* F */
    [70] = {0x7F,0x09,0x09,0x09,0x01},

    /* G */
    [71] = {0x3E,0x41,0x49,0x49,0x7A},

    /* H */
    [72] = {0x7F,0x08,0x08,0x08,0x7F},

    /* I */
    [73] = {0x00,0x41,0x7F,0x41,0x00},

    /* L */
    [76] = {0x7F,0x40,0x40,0x40,0x40},

    /* M */
    [77] = {0x7F,0x02,0x0C,0x02,0x7F},

    /* N */
    [78] = {0x7F,0x04,0x08,0x10,0x7F},

    /* O */
    [79] = {0x3E,0x41,0x41,0x41,0x3E},

    /* Q */
    [81] = {0x3E,0x41,0x51,0x21,0x5E},

    /* R */
    [82] = {0x7F,0x09,0x19,0x29,0x46},

    /* S */
    [83] = {0x46,0x49,0x49,0x49,0x31},

    /* T */
    [84] = {0x01,0x01,0x7F,0x01,0x01},

    /* U */
    [85] = {0x3F,0x40,0x40,0x40,0x3F},

    /* V */
    [86] = {0x1F,0x20,0x40,0x20,0x1F},

    /* Y */
    [89] = {0x03,0x04,0x78,0x04,0x03},

    /* g */
    [103] = {0x20,0x54,0x54,0x54,0x78},

    /* m */
    [109] = {0x7C,0x04,0x18,0x04,0x78},

    /* u */
    [117] = {0x3C,0x40,0x40,0x20,0x7C}
};


/* OLED framebuffer */

static uint8_t oled_buffer[128 * 64 / 8];


/* =========================================================
                    OLED FUNCTIONS
   ========================================================= */

static void oled_cmd(uint8_t command)
{
    uint8_t data[2];

    data[0] = 0x00;
    data[1] = command;

    i2c_master_transmit(
        oled_handle,
        data,
        sizeof(data),
        -1
    );
}


static void oled_data(uint8_t *data, size_t length)
{
    /*
     * I2C packet:
     *
     * first byte = 0x40
     * remaining = display data
     */

    uint8_t buffer[17];

    while (length > 0)
    {
        size_t chunk =
            length > 16 ? 16 : length;

        buffer[0] = 0x40;

        memcpy(
            &buffer[1],
            data,
            chunk
        );

        i2c_master_transmit(
            oled_handle,
            buffer,
            chunk + 1,
            -1
        );

        data += chunk;

        length -= chunk;
    }
}


static void oled_clear(void)
{
    memset(
        oled_buffer,
        0,
        sizeof(oled_buffer)
    );
}


static void oled_pixel(
    int x,
    int y,
    bool state
)
{
    if (x < 0 || x >= 128 ||
        y < 0 || y >= 64)
    {
        return;
    }

    int index =
        x + (y / 8) * 128;

    uint8_t mask =
        1 << (y % 8);

    if (state)
        oled_buffer[index] |= mask;
    else
        oled_buffer[index] &= ~mask;
}


static void oled_char(
    int x,
    int y,
    char c
)
{
    if ((unsigned char)c >= 128)
        return;

    for (int col = 0; col < 5; col++)
    {
        uint8_t line =
            font5x7[(unsigned char)c][col];

        for (int row = 0; row < 7; row++)
        {
            if (line & (1 << row))
            {
                oled_pixel(
                    x + col,
                    y + row,
                    true
                );
            }
        }
    }
}


static void oled_text(
    int x,
    int y,
    const char *text
)
{
    while (*text)
    {
        oled_char(
            x,
            y,
            *text
        );

        x += 6;

        if (x >= 128)
            break;

        text++;
    }
}


static void oled_update(void)
{
    for (int page = 0; page < 8; page++)
    {
        oled_cmd(
            0xB0 + page
        );

        oled_cmd(0x00);

        oled_cmd(0x10);

        oled_data(
            &oled_buffer[page * 128],
            128
        );
    }
}


static void oled_init(void)
{
    i2c_master_bus_config_t bus_config =
    {
        .i2c_port = I2C_NUM_0,

        .sda_io_num = OLED_SDA_GPIO,

        .scl_io_num = OLED_SCL_GPIO,

        .clk_source =
            I2C_CLK_SRC_DEFAULT,

        .glitch_ignore_cnt = 7,

        .flags.enable_internal_pullup = true
    };


    ESP_ERROR_CHECK(
        i2c_new_master_bus(
            &bus_config,
            &i2c_bus
        )
    );


    i2c_device_config_t device_config =
    {
        .dev_addr_length =
            I2C_ADDR_BIT_LEN_7,

        .device_address =
            OLED_I2C_ADDRESS,

        .scl_speed_hz =
            400000
    };


    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(
            i2c_bus,
            &device_config,
            &oled_handle
        )
    );


    /* SSD1306 initialization */

    oled_cmd(0xAE);

    oled_cmd(0xD5);
    oled_cmd(0x80);

    oled_cmd(0xA8);
    oled_cmd(0x3F);

    oled_cmd(0xD3);
    oled_cmd(0x00);

    oled_cmd(0x40);

    oled_cmd(0x8D);
    oled_cmd(0x14);

    oled_cmd(0x20);
    oled_cmd(0x00);

    oled_cmd(0xA1);

    oled_cmd(0xC8);

    oled_cmd(0xDA);
    oled_cmd(0x12);

    oled_cmd(0x81);
    oled_cmd(0xCF);

    oled_cmd(0xD9);
    oled_cmd(0xF1);

    oled_cmd(0xDB);
    oled_cmd(0x40);

    oled_cmd(0xA4);

    oled_cmd(0xA6);

    oled_cmd(0xAF);


    oled_clear();

    oled_update();


    ESP_LOGI(
        TAG,
        "SSD1306 OLED initialized"
    );
}


/* =========================================================
                    OLED MESSAGES
   ========================================================= */

static void oled_message(
    const char *line1,
    const char *line2,
    uint32_t duration_ms
)
{
    strncpy(
        oled_message_1,
        line1,
        sizeof(oled_message_1) - 1
    );

    oled_message_1[
        sizeof(oled_message_1) - 1
    ] = '\0';


    strncpy(
        oled_message_2,
        line2,
        sizeof(oled_message_2) - 1
    );

    oled_message_2[
        sizeof(oled_message_2) - 1
    ] = '\0';


    oled_message_until =
        (uint32_t)(
            esp_timer_get_time() / 1000
        ) + duration_ms;
}


/* =========================================================
                    SERVO
   ========================================================= */

static void servo_init(void)
{
    ledc_timer_config_t timer =
    {
        .speed_mode =
            LEDC_LOW_SPEED_MODE,

        .timer_num =
            LEDC_TIMER_0,

        .duty_resolution =
            LEDC_TIMER_16_BIT,

        .freq_hz = 50,

        .clk_cfg =
            LEDC_AUTO_CLK
    };


    ESP_ERROR_CHECK(
        ledc_timer_config(&timer)
    );


    ledc_channel_config_t channel =
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
        ledc_channel_config(&channel)
    );
}


static void servo_set_angle(
    int angle
)
{
    if (angle < 0)
        angle = 0;

    if (angle > 180)
        angle = 180;


    /*
     * Approximate servo pulse:
     *
     * 0 degree   = 0.5 ms
     * 180 degree = 2.5 ms
     *
     * Period = 20 ms
     */

    float pulse_us =
        500.0f +
        ((float)angle / 180.0f)
        * 2000.0f;


    uint32_t duty =
        (uint32_t)(
            (pulse_us / 20000.0f)
            * 65535.0f
        );


    ledc_set_duty(
        LEDC_LOW_SPEED_MODE,
        LEDC_CHANNEL_0,
        duty
    );


    ledc_update_duty(
        LEDC_LOW_SPEED_MODE,
        LEDC_CHANNEL_0
    );
}


/* =========================================================
                    FAN CONTROL
   ========================================================= */

static const char *fan_string(
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
            return "MED";

        case FAN_HIGH:
            return "HIGH";

        default:
            return "OFF";
    }
}


static void set_fan_speed(
    fan_speed_t speed
)
{
    fan_speed = speed;


    switch (speed)
    {
        case FAN_OFF:
            servo_set_angle(
                SERVO_OFF
            );
            break;

        case FAN_LOW:
            servo_set_angle(
                SERVO_LOW
            );
            break;

        case FAN_MEDIUM:
            servo_set_angle(
                SERVO_MEDIUM
            );
            break;

        case FAN_HIGH:
            servo_set_angle(
                SERVO_HIGH
            );
            break;
    }


    ESP_LOGI(
        TAG,
        "Fan = %s",
        fan_string(speed)
    );
}


/* =========================================================
                    BUZZER
   ========================================================= */

static void buzzer_beep(void)
{
    /*
     * Buzzer sounds ONLY when a touch button
     * is pressed.
     */

    gpio_set_level(
        BUZZER_GPIO,
        1
    );

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    gpio_set_level(
        BUZZER_GPIO,
        0
    );
}


/* =========================================================
                    ADC INITIALIZATION
   ========================================================= */

static void adc_init(void)
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
            ADC_BITWIDTH_12,

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


#if CONFIG_IDF_TARGET_ESP32

    adc_cali_line_fitting_config_t cali_config =
    {
        .unit_id = ADC_UNIT_1,

        .atten =
            ADC_ATTEN_DB_12,

        .bitwidth =
            ADC_BITWIDTH_12
    };


    if (
        adc_cali_create_scheme_line_fitting(
            &cali_config,
            &adc_cali_handle
        ) == ESP_OK
    )
    {
        adc_calibrated = true;

        ESP_LOGI(
            TAG,
            "ADC calibration enabled"
        );
    }

#endif
}


/* =========================================================
                    DUST READING
   ========================================================= */

static float read_dust_voltage(void)
{
    int raw = 0;

    int voltage_mv = 0;


    ESP_ERROR_CHECK(
        adc_oneshot_read(
            adc_handle,
            DUST_ADC_CHANNEL,
            &raw
        )
    );


    if (adc_calibrated)
    {
        adc_cali_raw_to_voltage(
            adc_cali_handle,
            raw,
            &voltage_mv
        );
    }
    else
    {
        voltage_mv =
            (raw * 3300) / 4095;
    }


    /*
     * ADC pin voltage
     */
    float adc_voltage =
        voltage_mv / 1000.0f;


    /*
     * Reverse the 10k / 20k divider:
     *
     * Vadc = Vsensor * 20/30
     *
     * Vsensor = Vadc * 1.5
     */

    float sensor_voltage =
        adc_voltage * 1.5f;


    return sensor_voltage;
}


/* =========================================================
                DUST CONCENTRATION
   ========================================================= */

static float calculate_dust(
    float voltage
)
{
    if (voltage <= DUST_VZERO)
        return 0.0f;


    float dust =
        (voltage - DUST_VZERO)
        * DUST_SENSITIVITY;


    if (dust < 0)
        dust = 0;


    return dust;
}


/* =========================================================
                    ESTIMATED AQI
   ========================================================= */

static int calculate_aqi(
    float pm
)
{
    /*
     * Approximate AQI.
     *
     * This is an ESTIMATED value because
     * GP2Y1010AU0F is not a certified
     * PM2.5 reference monitor.
     */

    if (pm <= 12.0f)
    {
        return (int)(
            pm * 50.0f / 12.0f
        );
    }


    if (pm <= 35.4f)
    {
        return 51 +
            (int)(
                (pm - 12.1f)
                * 49.0f
                / (35.4f - 12.1f)
            );
    }


    if (pm <= 55.4f)
    {
        return 101 +
            (int)(
                (pm - 35.5f)
                * 49.0f
                / (55.4f - 35.5f)
            );
    }


    if (pm <= 150.4f)
    {
        return 151 +
            (int)(
                (pm - 55.5f)
                * 49.0f
                / (150.4f - 55.5f)
            );
    }


    if (pm <= 250.4f)
    {
        return 201 +
            (int)(
                (pm - 150.5f)
                * 99.0f
                / (250.4f - 150.5f)
            );
    }


    if (pm <= 350.4f)
    {
        return 301 +
            (int)(
                (pm - 250.5f)
                * 99.0f
                / (350.4f - 250.5f)
            );
    }


    return 500;
}


/* =========================================================
                AUTO FAN CONTROL
   ========================================================= */

static void auto_control(void)
{
    if (dust_average <= 100.0f)
    {
        set_fan_speed(
            FAN_LOW
        );
    }
    else if (dust_average <= 300.0f)
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
}


/* =========================================================
                    TOUCH INIT
   ========================================================= */

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

        /*
         * TTP223 normally drives its OUT pin,
         * so internal pull-up is not required.
         */
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


/* =========================================================
                SPEED BUTTON
   ========================================================= */

static void speed_button(void)
{
    buzzer_beep();


    /*
     * Speed button switches to manual mode.
     */

    purifier_mode =
        MODE_MANUAL;


    switch (fan_speed)
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


    char line[22];

    snprintf(
        line,
        sizeof(line),
        "FAN: %s",
        fan_string(fan_speed)
    );


    oled_message(
        line,
        "MANUAL",
        2000
    );
}


/* =========================================================
                    AUTO BUTTON
   ========================================================= */

static void auto_button(void)
{
    buzzer_beep();


    if (purifier_mode ==
        MODE_AUTO)
    {
        purifier_mode =
            MODE_MANUAL;


        oled_message(
            "AUTO OFF",
            "",
            2000
        );
    }
    else
    {
        purifier_mode =
            MODE_AUTO;


        auto_control();


        char line[22];

        snprintf(
            line,
            sizeof(line),
            "FAN: %s",
            fan_string(fan_speed)
        );


        oled_message(
            "AUTO MODE",
            line,
            3000
        );
    }
}


/* =========================================================
                    TIMER BUTTON
   ========================================================= */

static void timer_button(void)
{
    buzzer_beep();


    switch (timer_mode)
    {
        case TIMER_OFF:

            timer_mode =
                TIMER_30_MIN;

            timer_remaining_seconds =
                30 * 60;

            oled_message(
                "TIMER: 30 MIN",
                "",
                2000
            );

            break;


        case TIMER_30_MIN:

            timer_mode =
                TIMER_60_MIN;

            timer_remaining_seconds =
                60 * 60;

            oled_message(
                "TIMER: 60 MIN",
                "",
                2000
            );

            break;


        case TIMER_60_MIN:

            timer_mode =
                TIMER_OFF;

            timer_remaining_seconds =
                0;

            oled_message(
                "TIMER OFF",
                "",
                2000
            );

            break;
    }
}


/* =========================================================
                    TOUCH TASK
   ========================================================= */

static void touch_task(
    void *arg
)
{
    int last_speed =
        !TOUCH_ACTIVE_LEVEL;

    int last_auto =
        !TOUCH_ACTIVE_LEVEL;

    int last_timer =
        !TOUCH_ACTIVE_LEVEL;


    while (1)
    {
        int speed =
            gpio_get_level(
                SPEED_TOUCH_GPIO
            );


        int auto_state =
            gpio_get_level(
                AUTO_TOUCH_GPIO
            );


        int timer =
            gpio_get_level(
                TIMER_TOUCH_GPIO
            );


        /*
         * Detect rising/falling transition
         * into the active state.
         */

        if (
            speed == TOUCH_ACTIVE_LEVEL &&
            last_speed != TOUCH_ACTIVE_LEVEL
        )
        {
            speed_button();
        }


        if (
            auto_state == TOUCH_ACTIVE_LEVEL &&
            last_auto != TOUCH_ACTIVE_LEVEL
        )
        {
            auto_button();
        }


        if (
            timer == TOUCH_ACTIVE_LEVEL &&
            last_timer != TOUCH_ACTIVE_LEVEL
        )
        {
            timer_button();
        }


        last_speed =
            speed;

        last_auto =
            auto_state;

        last_timer =
            timer;


        /*
         * Debounce.
         */

        vTaskDelay(
            pdMS_TO_TICKS(50)
        );
    }
}


/* =========================================================
                    DUST TASK
   ========================================================= */

static void dust_task(
    void *arg
)
{
    float sum = 0;

    int sample_count = 0;


    while (1)
    {
        float voltage =
            read_dust_voltage();


        float dust =
            calculate_dust(
                voltage
            );


        sum += dust;

        sample_count++;


        ESP_LOGI(
            TAG,
            "Dust sample = %.2f ug/m3",
            dust
        );


        /*
         * 60 samples = 1 minute.
         */

        if (
            sample_count >=
            DUST_SAMPLES
        )
        {
            dust_average =
                sum /
                sample_count;


            estimated_aqi =
                calculate_aqi(
                    dust_average
                );


            ESP_LOGI(
                TAG,
                "================================"
            );


            ESP_LOGI(
                TAG,
                "1 minute average: %.2f ug/m3",
                dust_average
            );


            ESP_LOGI(
                TAG,
                "Estimated AQI: %d",
                estimated_aqi
            );


            /*
             * AUTO mode changes fan
             * after each one-minute average.
             */

            if (
                purifier_mode ==
                MODE_AUTO
            )
            {
                auto_control();
            }


            sum = 0;

            sample_count = 0;
        }


        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}


/* =========================================================
                    TIMER TASK
   ========================================================= */

static void timer_task(
    void *arg
)
{
    while (1)
    {
        if (
            timer_remaining_seconds
            > 0
        )
        {
            timer_remaining_seconds--;


            if (
                timer_remaining_seconds
                == 0
            )
            {
                /*
                 * Timer expired.
                 */

                set_fan_speed(
                    FAN_OFF
                );


                timer_mode =
                    TIMER_OFF;


                purifier_mode =
                    MODE_MANUAL;


                ESP_LOGI(
                    TAG,
                    "Timer finished - fan OFF"
                );


                /*
                 * NO BUZZER HERE.
                 *
                 * User requested buzzer
                 * only for touch presses.
                 */
            }
        }


        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}


/* =========================================================
                    OLED TASK
   ========================================================= */

static void oled_task(
    void *arg
)
{
    while (1)
    {
        oled_clear();


        /*
         * AQI ALWAYS visible.
         */

        char line[22];


        snprintf(
            line,
            sizeof(line),
            "AQI: %d",
            estimated_aqi
        );


        oled_text(
            0,
            0,
            line
        );


        /*
         * Dust concentration.
         */

        snprintf(
            line,
            sizeof(line),
            "Dust: %.0f",
            dust_average
        );


        oled_text(
            0,
            12,
            line
        );


        /*
         * If a temporary event message
         * exists, show it.
         */

        uint32_t now =
            (uint32_t)(
                esp_timer_get_time()
                / 1000
            );


        if (
            now <
            oled_message_until
        )
        {
            oled_text(
                0,
                30,
                oled_message_1
            );


            oled_text(
                0,
                42,
                oled_message_2
            );
        }


        oled_update();


        vTaskDelay(
            pdMS_TO_TICKS(200)
        );
    }
}


/* =========================================================
                    WIFI
   ========================================================= */

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (
        event_base ==
        WIFI_EVENT &&
        event_id ==
        WIFI_EVENT_STA_START
    )
    {
        esp_wifi_connect();
    }


    else if (
        event_base ==
        WIFI_EVENT &&
        event_id ==
        WIFI_EVENT_STA_DISCONNECTED
    )
    {
        wifi_connected = false;

        esp_wifi_connect();

        ESP_LOGI(
            TAG,
            "Wi-Fi disconnected"
        );
    }


    else if (
        event_base ==
        IP_EVENT &&
        event_id ==
        IP_EVENT_STA_GOT_IP
    )
    {
        wifi_connected = true;

        ESP_LOGI(
            TAG,
            "Wi-Fi connected"
        );
    }
}


static void wifi_init(void)
{
    ESP_ERROR_CHECK(
        esp_netif_init()
    );


    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );


    esp_netif_create_default_wifi_sta();


    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
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


    wifi_config_t wifi_config = {};


    strcpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID
    );


    strcpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD
    );


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


    ESP_ERROR_CHECK(
        esp_wifi_start()
    );
}


/* =========================================================
                    BLYNK
   ========================================================= */

/*
 * Blynk datastream assignment:
 *
 * V0 = AQI
 * V1 = Dust
 * V2 = Auto
 * V3 = Speed
 * V4 = Timer
 * V5 = Fan
 * V6 = Timer remaining
 *
 *
 * V2:
 *     0 = manual
 *     1 = auto
 *
 * V3:
 *     0 = OFF
 *     1 = LOW
 *     2 = MEDIUM
 *     3 = HIGH
 *
 * V4:
 *     0 = OFF
 *     30 = 30 min
 *     60 = 60 min
 */


/*
 * Send value to Blynk.
 */

static void blynk_update(
    int pin,
    const char *value
)
{
    if (!wifi_connected)
        return;


    char url[300];


    snprintf(
        url,
        sizeof(url),
        "https://blynk.cloud/external/api/update?token=%s&V%d=%s",
        BLYNK_TOKEN,
        pin,
        value
    );


    esp_http_client_config_t config =
    {
        .url = url,

        .method =
            HTTP_METHOD_GET,

        .timeout_ms = 5000
    };


    esp_http_client_handle_t client =
        esp_http_client_init(
            &config
        );


    if (client)
    {
        esp_http_client_perform(
            client
        );

        esp_http_client_cleanup(
            client
        );
    }
}


/*
 * Send all current values.
 */

static void blynk_send_data(void)
{
    char value[32];


    snprintf(
        value,
        sizeof(value),
        "%d",
        estimated_aqi
    );

    blynk_update(
        0,
        value
    );


    snprintf(
        value,
        sizeof(value),
        "%.1f",
        dust_average
    );

    blynk_update(
        1,
        value
    );


    snprintf(
        value,
        sizeof(value),
        "%d",
        purifier_mode == MODE_AUTO
            ? 1
            : 0
    );

    blynk_update(
        2,
        value
    );


    snprintf(
        value,
        sizeof(value),
        "%d",
        fan_speed
    );

    blynk_update(
        5,
        value
    );


    snprintf(
        value,
        sizeof(value),
        "%lu",
        (unsigned long)
        timer_remaining_seconds
    );

    blynk_update(
        6,
        value
    );
}


/*
 * Blynk task.
 *
 * This periodically uploads:
 *
 * AQI
 * Dust
 * Auto
 * Fan
 * Timer remaining
 */

static void blynk_task(
    void *arg
)
{
    while (1)
    {
        blynk_send_data();


        vTaskDelay(
            pdMS_TO_TICKS(5000)
        );
    }
}


/* =========================================================
                    APP MAIN
   ========================================================= */

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "================================"
    );

    ESP_LOGI(
        TAG,
        "      ESP32 AIR PURIFIER"
    );

    ESP_LOGI(
        TAG,
        "================================"
    );


    /*
     * NVS is required for Wi-Fi.
     */

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

        ESP_ERROR_CHECK(
            nvs_flash_init()
        );
    }


    /* Buzzer */

    gpio_config_t buzzer_config =
    {
        .pin_bit_mask =
            1ULL << BUZZER_GPIO,

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
            &buzzer_config
        )
    );


    gpio_set_level(
        BUZZER_GPIO,
        0
    );


    /* Initialize modules */

    touch_init();

    servo_init();

    adc_init();

    oled_init();


    /*
     * Initial fan state:
     * OFF
     */

    set_fan_speed(
        FAN_OFF
    );


    /*
     * Wi-Fi
     */

    wifi_init();


    /*
     * Tasks
     */

    xTaskCreate(
        touch_task,
        "touch_task",
        4096,
        NULL,
        6,
        NULL
    );


    xTaskCreate(
        dust_task,
        "dust_task",
        4096,
        NULL,
        5,
        NULL
    );


    xTaskCreate(
        timer_task,
        "timer_task",
        2048,
        NULL,
        4,
        NULL
    );


    xTaskCreate(
        oled_task,
        "oled_task",
        4096,
        NULL,
        3,
        NULL
    );


    xTaskCreate(
        blynk_task,
        "blynk_task",
        4096,
        NULL,
        2,
        NULL
    );


    ESP_LOGI(
        TAG,
        "Air purifier started"
    );
}