/*  LED Lightbulb demo implementation using RGB LED

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/rmt.h>

#include <iot_button.h>
#include <led_strip.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>

#include "app_priv.h"

#define RMT_TX_CHANNEL RMT_CHANNEL_0
// #define RMT_TX_CHANNEL 8
/* This is the button that is used for toggling the power */
#define BUTTON_GPIO 0
#define BUTTON_ACTIVE_LEVEL 0
/* This is the GPIO on which the power will be set */
#define OUTPUT_GPIO 19

typedef struct
{
    uint16_t g_hue;
    uint16_t g_saturation;
    uint16_t g_value;
    bool g_power;
} st_led_config;

static st_led_config g_led[4];
// static uint16_t g_hue = DEFAULT_HUE;
// static uint16_t g_saturation = DEFAULT_SATURATION;
// static uint16_t g_value = DEFAULT_BRIGHTNESS;
// static bool g_power = DEFAULT_POWER;
static led_strip_t *g_strip;

static const char *TAG = "app_driver";

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
static void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i)
    {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

static esp_err_t app_light_set_led(uint8_t ch, uint32_t hue, uint32_t saturation, uint32_t brightness)
{
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    g_led[ch].g_hue = hue;
    g_led[ch].g_saturation = saturation;
    g_led[ch].g_value = brightness;
    led_strip_hsv2rgb(g_led[ch].g_hue, g_led[ch].g_saturation, g_led[ch].g_value, &red, &green, &blue);
    g_strip->set_pixel(g_strip, ch, red, green, blue);
    g_strip->refresh(g_strip, 100);
    return ESP_OK;
}
esp_err_t app_light_set(uint8_t ch, uint32_t hue, uint32_t saturation, uint32_t brightness)
{
    /* Whenever this function is called, light power will be ON */
    if (!g_led[ch].g_power)
    {
        g_led[ch].g_power = true;

        if (ch == 0)
        {
            esp_rmaker_update_param("CH1", ESP_RMAKER_DEF_POWER_NAME, esp_rmaker_bool(g_led[ch].g_power));
        }
        else if (ch == 1)
        {
            esp_rmaker_update_param("CH2", ESP_RMAKER_DEF_POWER_NAME, esp_rmaker_bool(g_led[ch].g_power));
        }
        else if (ch == 2)
        {
            esp_rmaker_update_param("CH3", ESP_RMAKER_DEF_POWER_NAME, esp_rmaker_bool(g_led[ch].g_power));
        }
        else if (ch == 3)
        {
            esp_rmaker_update_param("CH4", ESP_RMAKER_DEF_POWER_NAME, esp_rmaker_bool(g_led[ch].g_power));
        }
    }
    return app_light_set_led(ch, hue, saturation, brightness);
}

esp_err_t app_light_set_power(uint8_t ch, bool power)
{
    g_led[ch].g_power = power;
    if (power)
    {
        app_light_set(ch, g_led[ch].g_hue, g_led[ch].g_saturation, g_led[ch].g_value);
    }
    else
    {
        g_strip->set_pixel(g_strip, ch, 0, 0, 0);
        g_strip->refresh(g_strip, 100);
        // g_strip->clear(g_strip, 100);
        // Turn off
    }
    return ESP_OK;
}

esp_err_t app_light_set_brightness(uint8_t ch, uint16_t brightness)
{
    g_led[ch].g_value = brightness;
    return app_light_set(ch, g_led[ch].g_hue, g_led[ch].g_saturation, g_led[ch].g_value);
}
esp_err_t app_light_set_hue(uint8_t ch, uint16_t hue)
{
    g_led[ch].g_hue = hue;
    return app_light_set(ch, g_led[ch].g_hue, g_led[ch].g_saturation, g_led[ch].g_value);
}
esp_err_t app_light_set_saturation(uint8_t ch, uint16_t saturation)
{
    g_led[ch].g_saturation = saturation;
    return app_light_set(ch, g_led[ch].g_hue, g_led[ch].g_saturation, g_led[ch].g_value);
}

esp_err_t app_light_init(void)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_APP_LED_GPIO, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(CONFIG_APP_LED_GPIO, (led_strip_dev_t)config.channel);
    g_strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!g_strip)
    {
        ESP_LOGE(TAG, "install WS2812 driver failed");
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        g_led[i].g_hue = DEFAULT_HUE;
        g_led[i].g_saturation = DEFAULT_SATURATION;
        g_led[i].g_value = DEFAULT_BRIGHTNESS;
        g_led[i].g_power = DEFAULT_POWER;

        app_light_set_led(i, DEFAULT_HUE, DEFAULT_SATURATION, DEFAULT_BRIGHTNESS);
    }

    // g_strip->clear(g_strip, 100);
    return ESP_OK;
}

static void push_btn_cb(void *arg)
{
    app_light_set_power(0, !g_led[0].g_power);
    esp_rmaker_update_param("CH1", "power", esp_rmaker_bool(g_led[0].g_power));
}

static void button_press_3sec_cb(void *arg)
{
    nvs_flash_deinit();
    nvs_flash_erase();
    esp_restart();
}

void app_driver_init()
{
    app_light_init();
    button_handle_t btn_handle = iot_button_create(BUTTON_GPIO, BUTTON_ACTIVE_LEVEL);
    if (btn_handle)
    {
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, push_btn_cb, "RELEASE");
        iot_button_add_on_press_cb(btn_handle, 3, button_press_3sec_cb, NULL);
    }
}
