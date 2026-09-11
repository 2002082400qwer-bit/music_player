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
 * @brief  确保 SD 卡上存在与固件内嵌内容一致的 hello.wav。
 *
 * 行为约定：本函数是幂等的。首次运行会把内嵌音频写入卡中；
 * 后续启动若发现目标文件已存在且大小一致，则直接跳过写入，
 * 这样既避免每次上电重复写卡，也不会覆盖用户在卡上改过的同名文件
 * （尺寸一旦不同就会被重新写入）。
 *
 * @param[in]  mount_point  SD 卡挂载点，取板级初始化返回的 mount_point，例如 "/sdcard"
 *
 * @return
 *       - ESP_OK                写入成功，或文件已存在且大小一致（无需写入）
 *       - ESP_ERR_INVALID_ARG   mount_point 为 NULL
 *       - ESP_FAIL              打开文件失败、写入字节数不足，或回读校验不通过
 */
esp_err_t hello_wav_provision(const char *mount_point);

#ifdef __cplusplus
}
#endif
