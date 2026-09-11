/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化播放引擎：注册解封装器、创建播放器、订阅控制事件。
 *
 * 成功后播放引擎立即可用，并会主动发布一次当前状态，让界面显示"未找到音乐"或首曲歌名。
 *
 * @param[in]  codec  音频输出 codec 句柄，来自板级初始化的 playback_codec
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If codec is NULL
 *       - ESP_ERR_INVALID_STATE  If the engine is already started
 *       - ESP_FAIL               If extractors or the simple player cannot be created
 */
esp_err_t music_player_playback_init(esp_codec_dev_handle_t codec);

/**
 * @brief  停止播放并释放播放器、解封装器与事件订阅，可安全重复调用。
 */
void music_player_playback_deinit(void);

/**
 * @brief  读取播放进度（已播放毫秒数 + 总时长毫秒数），供界面进度条使用。
 *
 * 内部加锁读取，可被 UI 的定时器任务安全调用。
 * 曲目尚未就绪时两者都可能返回 0，界面据此隐藏进度。
 *
 * @param[out]  elapsed_ms   Elapsed playback time in milliseconds
 * @param[out]  duration_ms  Estimated track duration in milliseconds
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If elapsed_ms or duration_ms is NULL
 */
esp_err_t music_player_playback_get_progress(int *elapsed_ms, int *duration_ms);

#ifdef __cplusplus
}
#endif
