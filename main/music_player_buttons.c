/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "music_player_buttons.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "iot_button.h"
#include "app.h"
#include "music_player_config.h"

static const char *TAG = MUSIC_PLAYER_TAG_BUTTON;

#define MUSIC_PLAYER_BUTTON_LABEL_PLAY  "PLAY"

static bool s_buttons_inited = false;

/**
 * @brief ADC 按键组的事件回调：只把 PLAY 键的单击翻译成"播放/暂停切换"命令。
 *
 * @param[in]  button_handle  iot_button 句柄，用来查询本次触发的是哪种事件
 * @param[in]  usr_data       按键标签字符串（user_data 传 NULL 时由 board manager 填入）
 */
static void adc_button_event_cb(void *button_handle, void *usr_data)
{
    const char *label = (const char *)usr_data;
    if (label == NULL || strcmp(label, MUSIC_PLAYER_BUTTON_LABEL_PLAY) != 0) {
        return;
    }

    button_event_t event = iot_button_get_event((button_handle_t)button_handle);
    if (event != BUTTON_SINGLE_CLICK) {
        return;
    }

    esp_err_t ret = app_event_post(MUSIC_PLAYER_EVT_TOGGLE, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "播放键事件投递失败: %s", esp_err_to_name(ret));
    }
}

esp_err_t music_player_buttons_init(void)
{
    if (s_buttons_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_ADC_BUTTON_GROUP);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "板载按键组不可用: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_board_device_callback_register(ESP_BOARD_DEVICE_NAME_ADC_BUTTON_GROUP,
                                             (void *)adc_button_event_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "注册按键回调失败: %s", esp_err_to_name(ret));
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_ADC_BUTTON_GROUP);
        return ret;
    }

    s_buttons_inited = true;
    return ESP_OK;
}

void music_player_buttons_deinit(void)
{
    if (!s_buttons_inited) {
        return;
    }
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_ADC_BUTTON_GROUP);
    s_buttons_inited = false;
}
