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

typedef struct {
    esp_codec_dev_handle_t  playback_codec;
    char                    scan_dir[96];
    char                    mount_point[32];
} music_player_board_t;

/**
 * @brief  初始化 SD 卡与音频 DAC 设备。
 *
 * 成功后填好 board 结构：挂载点、扫描目录、以及可用来输出 PCM 的 codec 句柄。
 * 内部通过 ESP Board Manager 按设备名取设备，具体引脚与芯片型号由板级配置决定。
 *
 * @param[out]  board  Board context to receive mount point, scan directory, and playback codec handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If board is NULL
 *       - ESP_ERR_INVALID_STATE  If board resources are already initialized
 *       - ESP_FAIL               If a required board device handle is unavailable
 *       - Others                 Errors from board manager or codec device initialization
 **/
esp_err_t music_player_board_init(music_player_board_t *board);

/**
 * @brief  释放 music_player_board_init() 初始化过的板级外设。
 *
 * 可安全重复调用；即使只初始化成功了一半，也只会释放真正初始化过的部分。
 *
 * @param[in,out]  board  Board context returned by music_player_board_init(), or NULL to release tracked devices only
 */
void music_player_board_deinit(music_player_board_t *board);

#ifdef __cplusplus
}
#endif
