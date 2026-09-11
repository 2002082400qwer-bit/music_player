/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "dev_fs_fat.h"
#include "esp_codec_dev.h"
#include "music_player_board.h"
#include "music_player_config.h"

static const char *TAG = MUSIC_PLAYER_TAG_BOARD;

static bool s_ldo_inited = false;
static bool s_sdcard_inited = false;
static bool s_dac_inited = false;     /* 音频 DAC（codec）是否已初始化 */

esp_err_t music_player_board_init(music_player_board_t *board)
{
    ESP_RETURN_ON_FALSE(board != NULL, ESP_ERR_INVALID_ARG, TAG, "板级句柄为空");
    ESP_RETURN_ON_FALSE(!(s_sdcard_inited || s_dac_inited), ESP_ERR_INVALID_STATE, TAG, "板级外设已经初始化过");

    memset(board, 0, sizeof(*board));

    esp_err_t ret = ESP_OK;
#if CONFIG_IDF_TARGET_ESP32P4

    ESP_RETURN_ON_ERROR(esp_board_periph_init(ESP_BOARD_PERIPH_NAME_LDO_MIPI), TAG, "初始化 MIPI 屏供电失败");
    s_ldo_inited = true;
#endif

    ESP_GOTO_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD), err_cleanup, TAG,
                      "初始化 SD 卡失败");
    s_sdcard_inited = true;

    dev_fs_fat_handle_t *sd = NULL;
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_FS_SDCARD, (void **)&sd);
    ESP_GOTO_ON_FALSE(ret == ESP_OK && sd != NULL && sd->mount_point != NULL, ESP_FAIL, err_cleanup, TAG,
                      "取 SD 卡挂载点失败");
    snprintf(board->mount_point, sizeof(board->mount_point), "%s", sd->mount_point);
    snprintf(board->scan_dir, sizeof(board->scan_dir), "%s", sd->mount_point);

    ESP_GOTO_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC), err_cleanup, TAG,
                      "初始化音频 DAC 失败");
    s_dac_inited = true;

    dev_audio_codec_handles_t *play_dev = NULL;
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&play_dev);
    ESP_GOTO_ON_FALSE(ret == ESP_OK && play_dev != NULL && play_dev->codec_dev != NULL, ESP_FAIL, err_cleanup, TAG,
                      "取音频 codec 句柄失败");
    board->playback_codec = play_dev->codec_dev;

    ESP_GOTO_ON_ERROR(esp_codec_dev_set_out_vol(board->playback_codec, MUSIC_PLAYER_DEFAULT_VOLUME), err_cleanup,
                      TAG, "设置初始音量失败");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_AUDIO_SIMPLE_PLAYER_RESAMPLE_DEST_RATE,
        .channel = CONFIG_AUDIO_SIMPLE_PLAYER_CH_CVT_DEST,
#if CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_16BIT
        .bits_per_sample = 16,
#elif CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_24BIT
        .bits_per_sample = 24,
#elif CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_32BIT
        .bits_per_sample = 32,
#else
        .bits_per_sample = 16,
#endif
    };
    ESP_GOTO_ON_ERROR(esp_codec_dev_open(board->playback_codec, &fs), err_cleanup, TAG, "打开音频 codec 失败");

    return ESP_OK;

err_cleanup:
    music_player_board_deinit(board);
    return ret;
}

void music_player_board_deinit(music_player_board_t *board)
{

    if (!s_ldo_inited && !s_sdcard_inited && !s_dac_inited) {
        return;
    }

    if (board != NULL && board->playback_codec != NULL) {
        esp_codec_dev_close(board->playback_codec);
        board->playback_codec = NULL;
    }

    if (s_dac_inited) {
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        s_dac_inited = false;
    }
    if (s_sdcard_inited) {
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        s_sdcard_inited = false;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (s_ldo_inited) {
        esp_board_periph_deinit(ESP_BOARD_PERIPH_NAME_LDO_MIPI);
        s_ldo_inited = false;
    }
#endif
}
