/*
 * EcoAir — ESP32 Air Purifier Firmware (ESP-IDF v6.0+, no WiFi)
 *
 * Hardware:
 *   - ESP32 DevKit
 *   - GP2Y1010AU0F Sharp Dust Sensor (analog, LED-pulsed)
 *   - 0.96" OLED SSD1306 (I2C)
 *   - 4x TTP223 Touch Sensor Modules (Power, Speed, Auto, Timer)
 *   - SG90 Servo Motor (via LEDC PWM)
 *   - Buzzer
 *   - Heater control (optional)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

static const char *TAG = "ECOAIR";

/* ================= PINS ================= */
#define PIN_BTN_POWER   GPIO_NUM_14
#define PIN_BTN_SPEED   GPIO_NUM_12
#define PIN_BTN_AUTO    GPIO_NUM_15
#define PIN_BTN_TIMER   GPIO_NUM_3
#define PIN_BUZZER      GPIO_NUM_1
#define PIN_HEATER      GPIO_NUM_16
#define PIN_SERVO       GPIO_NUM_13
#define PIN_DUST_LED    GPIO_NUM_25
#define DUST_ADC_UNIT   ADC_UNIT_1
#define DUST_ADC_CHAN   ADC_CHANNEL_4   /* GPIO34 = ADC1_CH4 */

#define OLED_SDA        GPIO_NUM_4
#define OLED_SCL        GPIO_NUM_5
#define OLED_I2C_PORT   I2C_NUM_0
#define OLED_ADDR       0x3C

/* ================= SERVO ANGLES ================= */
#define ANG_OFF   5
#define ANG_LOW   45
#define ANG_MID   90
#define ANG_FAST  120
#define ANG_HIGH  178

/* ================= DUST SENSOR ================= */
#define VDIV_COMPENSATION  1.5f

/* ================= LEDC SERVO CONFIG ================= */
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL      LEDC_CHANNEL_0
#define LEDC_FREQ_HZ      50
#define LEDC_RESOLUTION   LEDC_TIMER_14_BIT
#define SERVO_MIN_US      500
#define SERVO_MAX_US      2400

/* ================= SYSTEM STATE ================= */
typedef struct {
    volatile bool isPoweredOn;
    volatile int  sysMode;         /* 1=AUTO, 0=MANUAL */
    volatile int  fanSpeedMode;    /* 1..4 */
    volatile int  timerHours;
    volatile int  pm25;
    volatile int  pm10;
    volatile float targetLife;
    volatile float displayedLife;
    char  lastAction[24];
    int64_t powerMsgTimerUs;
    int64_t timerStartUs;
} system_state_t;

static system_state_t g_state = {
    .isPoweredOn = true,
    .sysMode = 1,
    .fanSpeedMode = 1,
    .timerHours = 0,
    .pm25 = 0,
    .pm10 = 0,
    .targetLife = 100.0f,
    .displayedLife = 100.0f,
    .lastAction = {0},
    .powerMsgTimerUs = 0,
    .timerStartUs = 0,
};

/* ================= I2C + ADC HANDLES ================= */
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t oled_dev_handle = NULL;

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_cali_done = false;

/* ================= OLED FRAMEBUFFER + FONT ================= */
static uint8_t oled_fb[1024];

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x00,0x07,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41}, {0x02,0x01,0x01,0x01,0x02},
    {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x01,0x01}, {0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03},
    {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00}, {0x00,0x7F,0x10,0x28,0x44},
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x14,0x7C},
    {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x24,0x00},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},
};

#define FONT_FIRST_CHAR  32
#define FONT_LAST_CHAR   122
#define FONT_WIDTH       5
#define FONT_HEIGHT      7
#define CHAR_SPACING     1

/* ================= OLED I2C ================= */
static esp_err_t oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(oled_dev_handle, buf, sizeof(buf), -1);
}

static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF,
        0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    for (int i = 0; i < sizeof(init_seq); i++)
        oled_cmd(init_seq[i]);
}

static void oled_flush(void)
{
    uint8_t buf[1025];
    buf[0] = 0x40;
    memcpy(&buf[1], oled_fb, 1024);
    i2c_master_transmit(oled_dev_handle, buf, sizeof(buf), -1);
}

static void oled_clear(void)
{
    memset(oled_fb, 0, sizeof(oled_fb));
}

static void oled_set_pixel(int x, int y, int on)
{
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int page = y / 8;
    int bit  = y % 8;
    if (on)
        oled_fb[page * 128 + x] |=  (1 << bit);
    else
        oled_fb[page * 128 + x] &= ~(1 << bit);
}

static int oled_draw_char(int x, int y, char c, int size, int invert)
{
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) return x + (FONT_WIDTH + CHAR_SPACING) * size;
    const uint8_t *glyph = font5x7[c - FONT_FIRST_CHAR];
    for (int col = 0; col < FONT_WIDTH; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < FONT_HEIGHT; row++) {
            int on = (line >> row) & 1;
            if (invert) on = !on;
            if (size == 1) {
                oled_set_pixel(x + col, y + row, on);
            } else {
                for (int dx = 0; dx < size; dx++)
                    for (int dy = 0; dy < size; dy++)
                        oled_set_pixel(x + col*size + dx, y + row*size + dy, on);
            }
        }
    }
    return x + (FONT_WIDTH + CHAR_SPACING) * size;
}

static int oled_draw_string(int x, int y, const char *str, int size, int invert)
{
    while (*str) {
        if (invert) {
            int w = (FONT_WIDTH + CHAR_SPACING) * size;
            int h = FONT_HEIGHT * size;
            for (int dx = 0; dx < w; dx++)
                for (int dy = 0; dy < h; dy++)
                    oled_set_pixel(x + dx, y + dy, 1);
        }
        x = oled_draw_char(x, y, *str, size, invert);
        str++;
    }
    return x;
}

static int oled_draw_int(int x, int y, int val, int size)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", val);
    return oled_draw_string(x, y, buf, size, 0);
}

/* ================= I2C BUS INIT ================= */
static void i2c_init_bus(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&bus_cfg, &i2c_bus_handle);

    i2c_device_config_t dev_cfg = {
        .device_address = OLED_ADDR,
        .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &oled_dev_handle);
}

/* ================= ADC INIT ================= */
static void adc_init_all(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = DUST_ADC_UNIT,
    };
    adc_oneshot_new_unit(&unit_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, DUST_ADC_CHAN, &chan_cfg);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORT
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = DUST_ADC_UNIT,
        .chan = DUST_ADC_CHAN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle);
    adc_cali_done = true;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORT
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = DUST_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_line_fitting(&cali_cfg, &adc_cali_handle);
    adc_cali_done = true;
#endif
}

/* ================= DUST SENSOR ================= */
static int read_dust_sensor(void)
{
    gpio_set_level(PIN_DUST_LED, 0);
    esp_rom_delay_us(280);

    int raw = 0;
    adc_oneshot_read(adc_handle, DUST_ADC_CHAN, &raw);

    esp_rom_delay_us(40);
    gpio_set_level(PIN_DUST_LED, 1);

    int mv = 0;
    if (adc_cali_done) {
        adc_cali_raw_to_voltage(adc_cali_handle, raw, &mv);
    } else {
        mv = raw * 3300 / 4095;
    }

    float measuredV = mv / 1000.0f;
    float actualV = measuredV * VDIV_COMPENSATION;
    float density = (0.17f * actualV - 0.1f) * 1000.0f;
    if (density < 0) density = 0;
    return (int)density;
}

/* ================= BUZZER ================= */
static void beep(void)
{
    gpio_set_level(PIN_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(PIN_BUZZER, 0);
}

/* ================= SERVO ================= */
static void servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PIN_SERVO,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_cfg);
}

static inline uint32_t angle_to_duty(int angle)
{
    float pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * (angle / 180.0f);
    float duty = (pulse_us / 20000.0f) * 16383.0f;
    return (uint32_t)duty;
}

static void servo_write(int angle)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(angle));
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/* ================= GPIO INIT ================= */
static void gpio_init_all(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_POWER) | (1ULL << PIN_BTN_SPEED) |
                        (1ULL << PIN_BTN_AUTO)  | (1ULL << PIN_BTN_TIMER),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER) | (1ULL << PIN_HEATER) | (1ULL << PIN_DUST_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    gpio_set_level(PIN_DUST_LED, 1);
    gpio_set_level(PIN_BUZZER, 0);
    gpio_set_level(PIN_HEATER, 1);
}

/* ================= NVS ================= */
static void load_filter_life(void)
{
    nvs_handle_t h;
    if (nvs_open("ecoair", NVS_READWRITE, &h) == ESP_OK) {
        float life = 100.0f;
        nvs_get_float(h, "filterLife", &life);
        if (life < 0 || life > 100) life = 100.0f;
        g_state.targetLife = life;
        g_state.displayedLife = life;
        nvs_close(h);
    }
}

__attribute__((unused))
static void save_filter_life(float life)
{
    nvs_handle_t h;
    if (nvs_open("ecoair", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_float(h, "filterLife", life);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ================= COMMAND HANDLER ================= */
static void set_last_action(const char *msg)
{
    strncpy(g_state.lastAction, msg, sizeof(g_state.lastAction) - 1);
    g_state.lastAction[sizeof(g_state.lastAction) - 1] = '\0';
}

static void handle_command(char c)
{
    switch (c) {
    case '1':   /* power toggle */
        g_state.isPoweredOn = !g_state.isPoweredOn;
        set_last_action(g_state.isPoweredOn ? "POWER ON" : "POWER OFF");
        g_state.powerMsgTimerUs = esp_timer_get_time();
        beep();
        break;

    case '2':   /* speed cycle */
        if (g_state.isPoweredOn) {
            g_state.sysMode = 0;
            g_state.fanSpeedMode++;
            if (g_state.fanSpeedMode > 4) g_state.fanSpeedMode = 1;
            const char *labels[] = {"FAN LOW","FAN MID","FAN FAST","FAN MAX"};
            set_last_action(labels[g_state.fanSpeedMode - 1]);
            beep();
        }
        break;

    case '3':   /* auto/manual toggle */
        if (g_state.isPoweredOn) {
            g_state.sysMode = !g_state.sysMode;
            set_last_action(g_state.sysMode ? "AUTO ON" : "AUTO OFF");
            beep();
        }
        break;

    case '4':   /* timer cycle (0->2->4->6->8->0) */
        if (g_state.isPoweredOn) {
            g_state.timerHours += 2;
            if (g_state.timerHours > 8) g_state.timerHours = 0;
            if (g_state.timerHours == 0) {
                set_last_action("TIMER OFF");
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "TIMER %dH", g_state.timerHours);
                set_last_action(buf);
                g_state.timerStartUs = esp_timer_get_time();
            }
            beep();
        }
        break;

    default:
        break;
    }
}

/* ================= BUTTON TASK ================= */
static void button_task(void *arg)
{
    int64_t last_press_us = 0;
    const int64_t debounce_us = 120000;  /* 120 ms */

    while (1) {
        int64_t now = esp_timer_get_time();
        if (now - last_press_us >= debounce_us) {
            if (gpio_get_level(PIN_BTN_POWER)) { handle_command('1'); last_press_us = now; }
            if (gpio_get_level(PIN_BTN_SPEED)) { handle_command('2'); last_press_us = now; }
            if (gpio_get_level(PIN_BTN_AUTO))  { handle_command('3'); last_press_us = now; }
            if (gpio_get_level(PIN_BTN_TIMER)) { handle_command('4'); last_press_us = now; }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ================= DISPLAY ================= */
static void draw_screen(void)
{
    oled_clear();

    /* Top bar: mode */
    oled_draw_string(0, 0,
        !g_state.isPoweredOn ? "POWER OFF" :
        (g_state.sysMode ? "AUTO MODE" : "MANUAL"), 1, 0);

    /* Timer indicator (top right) */
    if (g_state.timerHours > 0 && g_state.isPoweredOn) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%dH", g_state.timerHours);
        oled_draw_string(90, 0, tbuf, 1, 0);
    }

    /* AQI big number */
    oled_draw_string(0, 16, "AQI ", 2, 0);
    oled_draw_int(50, 16, g_state.pm25, 2);

    /* PM2.5 line */
    oled_draw_string(0, 36, "PM2.5:", 1, 0);
    oled_draw_int(42, 36, g_state.pm25, 1);

    /* PM10 line */
    oled_draw_string(0, 46, "PM10 :", 1, 0);
    oled_draw_int(42, 46, g_state.pm10, 1);

    /* Filter life + fan speed */
    oled_draw_string(0, 56, "F:", 1, 0);
    oled_draw_int(12, 56, (int)g_state.displayedLife, 1);
    oled_draw_string(30, 56, "% ", 1, 0);

    const char *speed_labels[] = {"LOW","MID","FAST","MAX"};
    if (g_state.fanSpeedMode >= 1 && g_state.fanSpeedMode <= 4)
        oled_draw_string(42, 56, speed_labels[g_state.fanSpeedMode - 1], 1, 0);

    oled_flush();

    /* Action toast overlay */
    if (g_state.lastAction[0] != '\0') {
        vTaskDelay(pdMS_TO_TICKS(80));
        oled_clear();
        oled_draw_string(5, 25, g_state.lastAction, 2, 0);
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(300));
        g_state.lastAction[0] = '\0';
    }
}

/* ================= MAIN TASK ================= */
static void main_task(void *arg)
{
    int64_t last_dust_us = 0;

    /* Boot screen */
    oled_clear();
    oled_draw_string(0, 10, "EcoAir", 2, 1);  /* inverted = highlight */
    oled_draw_string(32, 36, "Air 1", 2, 0);
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(800));

    while (1) {
        /* ---- Read dust sensor every 1 second ---- */
        int64_t now = esp_timer_get_time();
        if (now - last_dust_us > 1000000) {
            int reading = read_dust_sensor();
            g_state.pm25 = reading;
            g_state.pm10 = (int)(reading * 1.5f);
            last_dust_us = now;
        }

        /* ---- Smooth filter life animation ---- */
        float diff = g_state.displayedLife - g_state.targetLife;
        if (diff < 0) diff = -diff;
        if (diff > 0.1f)
            g_state.displayedLife = g_state.displayedLife * 0.99f + g_state.targetLife * 0.01f;
        else
            g_state.displayedLife = g_state.targetLife;

        /* ---- Power-off display ---- */
        if (!g_state.isPoweredOn) {
            servo_write(ANG_OFF);
            int64_t elapsed_ms = (esp_timer_get_time() - g_state.powerMsgTimerUs) / 1000;
            if (elapsed_ms < 600) {
                oled_clear();
                oled_draw_string(10, 25, "POWER OFF", 2, 0);
                oled_flush();
            } else {
                oled_clear();
                oled_flush();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* ---- Timer auto-shutdown ---- */
        if (g_state.timerHours > 0) {
            int64_t elapsed_us = esp_timer_get_time() - g_state.timerStartUs;
            if (elapsed_us > (int64_t)g_state.timerHours * 3600000000LL) {
                g_state.isPoweredOn = false;
                g_state.powerMsgTimerUs = esp_timer_get_time();
                set_last_action("POWER OFF");
            }
        }

        /* ---- Fan/servo logic ---- */
        if (g_state.sysMode == 1) {     /* AUTO */
            if      (g_state.pm25 > 150) { servo_write(ANG_HIGH); g_state.fanSpeedMode = 4; }
            else if (g_state.pm25 > 100) { servo_write(ANG_FAST); g_state.fanSpeedMode = 3; }
            else if (g_state.pm25 > 50)  { servo_write(ANG_MID);  g_state.fanSpeedMode = 2; }
            else                          { servo_write(ANG_LOW);  g_state.fanSpeedMode = 1; }
        } else {                        /* MANUAL */
            int angles[] = {ANG_LOW, ANG_MID, ANG_FAST, ANG_HIGH};
            servo_write(angles[g_state.fanSpeedMode - 1]);
        }

        draw_screen();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ================= APP MAIN ================= */
void app_main(void)
{
    /* NVS init (needed for filter life storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Initialize all hardware */
    gpio_init_all();
    i2c_init_bus();
    oled_init();
    adc_init_all();
    servo_init();
    load_filter_life();

    ESP_LOGI(TAG, "EcoAir started — no WiFi, buttons + OLED only");

    /* Create FreeRTOS tasks */
    xTaskCreate(button_task,  "button_task",  4096, NULL, 5, NULL);
    xTaskCreate(main_task,    "main_task",    8192, NULL, 4, NULL);

    /* Power-on beep */
    beep();
}
