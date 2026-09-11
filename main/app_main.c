/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app.h"

/**
 * @brief  ESP-IDF 的应用入口。
 *
 * 调用 app_start() 之后本函数就返回了：初始化完成后，
 * 事件任务 mp_evt、LVGL 任务与播放器任务会继续运行，主任务无需常驻。
 */
void app_main(void)
{
    app_start();
}
