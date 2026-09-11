/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化板载按键并注册 PLAY 键回调。
 *
 * 内部按设备名初始化 board manager 的 ADC 按键组设备，并注册单击事件回调。
 * 板上没有按键设备时返回错误码，调用方可以选择忽略，不影响屏幕按键功能。
 *
 * @return
 *       - ESP_OK                 成功
 *       - ESP_ERR_INVALID_STATE  已经初始化过
 *       - 其它                    board manager 初始化或回调注册失败的错误码
 */
esp_err_t music_player_buttons_init(void);

/**
 * @brief  释放板载按键设备（注销回调由设备反初始化统一完成）。
 */
void music_player_buttons_deinit(void);

#ifdef __cplusplus
}
#endif
