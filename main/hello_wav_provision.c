/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "hello_wav_provision.h"
#include "music_player_config.h"

static const char *TAG = MUSIC_PLAYER_TAG_BOARD;

extern const uint8_t hello_wav_start[] asm("_binary_hello_wav_start");
extern const uint8_t hello_wav_end[]   asm("_binary_hello_wav_end");

/**
 * @brief  读取文件大小，用于判断是否需要写入以及写完后的回读校验。
 *
 * @param[in]  path  文件路径（VFS 路径，例如 "/sdcard/hello.wav"）
 *
 * @return 文件字节数；文件不存在或无法访问时返回 -1
 */
static long hello_wav_get_size(const char *path)
{
    struct stat st = {0};

    if (stat(path, &st) != 0) {
        return -1;
    }

    return (long)st.st_size;
}

esp_err_t hello_wav_provision(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(mount_point != NULL, ESP_ERR_INVALID_ARG, TAG, "SD 卡挂载点为空");

    size_t expected = (size_t)(hello_wav_end - hello_wav_start);

    char path[128] = {0};
    snprintf(path, sizeof(path), "%s/hello.wav", mount_point);

    long existing = hello_wav_get_size(path);
    if (existing == (long)expected) {
        return ESP_OK;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "打开 %s 失败", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(hello_wav_start, 1, expected, fp);

    fflush(fp);
    fclose(fp);

    if (written != expected) {
        ESP_LOGE(TAG, "写入不完整：%u/%u 字节", (unsigned)written, (unsigned)expected);
        return ESP_FAIL;
    }

    long verify = hello_wav_get_size(path);
    if (verify != (long)expected) {
        ESP_LOGE(TAG, "回读校验失败：%ld/%u 字节", verify, (unsigned)expected);
        return ESP_FAIL;
    }

    return ESP_OK;
}
