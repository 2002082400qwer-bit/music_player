/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  创建自绘的播放器界面，订阅播放状态事件。
 *
 * 内部会在 LVGL 锁的保护下建好整个页面（顶部歌名/模式/音量、
 * 中间进度条与时间、底部播放控制栏），随后启动 LVGL 任务，
 * 最后订阅 MUSIC_PLAYER_EVT_STATE_CHANGED，之后所有界面刷新都由事件驱动。
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If UI resources are already initialized
 *       - Others                 Errors from display lock, LVGL adapter start, or event subscription
 */
esp_err_t music_player_ui_init(void);

/**
 * @brief  销毁 music_player_ui_init() 创建的所有界面资源。
 */
void music_player_ui_deinit(void);

#ifdef __cplusplus
}
#endif
