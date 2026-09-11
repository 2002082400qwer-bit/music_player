/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 esp_lvgl_adapter、LCD、触摸，并挂载资源文件系统。
 *
 * 这里只把"显示通路"准备好（含把 assets 分区挂成 LVGL 的 F: 盘，
 * 供 FreeType 读取 font.ttf），具体界面控件由 music_player_ui.c 创建。
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If display resources are already initialized
 *       - ESP_FAIL               If display, touch, or LVGL adapter registration fails
 *       - Others                 Errors from board manager or LVGL adapter initialization
 */
esp_err_t music_player_display_init(void);

/**
 * @brief  在界面控件创建完成之后，启动 LVGL 适配层任务。
 *
 * 必须放在控件创建之后：LVGL 任务一旦跑起来就会开始刷屏，
 * 此时如果控件还没建好，屏幕上会先出现空白或错位。
 *
 * @return
 *       - ESP_OK                 On success or if already started
 *       - ESP_ERR_INVALID_STATE  If display resources are not initialized
 *       - Others                 Errors from LVGL adapter start
 */
esp_err_t music_player_display_start(void);

/**
 * @brief  释放显示与 LVGL 适配层资源，并卸载资源文件系统。
 */
void music_player_display_deinit(void);

/**
 * @brief  在持有 LVGL 适配层锁的前提下执行回调。
 *
 * LVGL 不是线程安全的，凡是非 LVGL 任务想碰控件，都必须通过本函数进入，
 * 否则会出现偶发的花屏或崩溃。
 *
 * @param[in]  cb   Callback to run while the LVGL adapter is locked
 * @param[in]  ctx  User context passed to the callback
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If cb is NULL
 *       - ESP_ERR_INVALID_STATE  If the LVGL adapter is not initialized
 *       - ESP_FAIL               If locking the LVGL adapter fails
 */
esp_err_t music_player_display_lock_run(void (*cb)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
