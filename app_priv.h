/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_POWER true
#define DEFAULT_HUE 180
#define DEFAULT_SATURATION 100
#define DEFAULT_BRIGHTNESS 25

void app_driver_init(void);
esp_err_t app_light_set(uint8_t ch, uint32_t hue, uint32_t saturation, uint32_t brightness);
esp_err_t app_light_set_power(uint8_t ch, bool power);
esp_err_t app_light_set_brightness(uint8_t ch, uint16_t brightness);
esp_err_t app_light_set_hue(uint8_t ch, uint16_t hue);
esp_err_t app_light_set_saturation(uint8_t ch, uint16_t saturation);
