/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "music_player_display.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "esp_lv_adapter.h"
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS
#include "esp_lv_fs.h"
#include "esp_mmap_assets.h"
#endif
#include "music_player_config.h"

static const char *TAG = MUSIC_PLAYER_TAG_DISPLAY;

static lv_display_t *s_disp = NULL;
static lv_indev_t *s_touch = NULL;

static bool s_lcd_board_inited = false;
static bool s_touch_board_inited = false;
static bool s_adapter_inited = false;
static bool s_touch_registered = false;
static bool s_display_inited = false;
static bool s_started = false;
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS
static esp_lv_fs_handle_t s_fs_handle = NULL;
static mmap_assets_handle_t s_assets = NULL;
#endif  /* CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS */

static void unmount_assets_fs(void);

/**
 * @brief 释放板级 LCD 与触摸设备。
 *
 * 释放顺序与初始化相反：先摘触摸，再摘 LCD。
 */
static void display_release_board_devices(void)
{
    if (s_touch_board_inited) {
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH);
        s_touch_board_inited = false;
    }
    if (s_lcd_board_inited) {
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
        s_lcd_board_inited = false;
    }
}

/**
 * @brief 释放 LVGL 适配层相关资源。
 *
 * 顺序很关键：先卸载资源文件系统，再注销触摸与显示，最后才 deinit 适配层。
 * 反过来会在对象已被销毁后还去引用它。
 */
static void display_release_adapter(void)
{
    s_started = false;
    unmount_assets_fs();
    if (s_touch_registered && s_touch != NULL) {
        esp_lv_adapter_unregister_touch(s_touch);
        s_touch = NULL;
        s_touch_registered = false;
    }
    if (s_disp != NULL) {
        esp_lv_adapter_unregister_display(s_disp);
        s_disp = NULL;
    }
    if (s_adapter_inited) {
        esp_lv_adapter_deinit();
        s_adapter_inited = false;
    }
}

esp_err_t music_player_display_lock_run(void (*cb)(void *ctx), void *ctx)
{
    ESP_RETURN_ON_FALSE(cb != NULL, ESP_ERR_INVALID_ARG, TAG, "回调为空");
    ESP_RETURN_ON_FALSE(s_adapter_inited, ESP_ERR_INVALID_STATE, TAG, "显示适配层尚未初始化");

    esp_err_t ret = esp_lv_adapter_lock(pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }
    cb(ctx);
    esp_lv_adapter_unlock();
    return ESP_OK;
}

/**
 * @brief 把 assets 分区挂载成 LVGL 可访问的 F: 盘。
 *
 * 分区内容由构建脚本从工程的 assets/ 目录打包而来（见 main/CMakeLists.txt），
 * 本板上只有一个 font.ttf。使用 mmap 方式意味着字体数据不需要拷进 RAM，
 * 而是按需从 flash 映射，能显著省内存。
 *
 * 注意：找不到分区或挂载失败都只告警、不报错返回，
 * 因为界面可以退化使用 LVGL 内置字体，播放功能不该因此起不来。
 */
static esp_err_t mount_assets_fs(void)
{
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS
    const mmap_assets_config_t mmap_cfg = {
        .partition_label = "assets",
        .max_files = 1,
        .checksum = 0,
        .flags = {
            .mmap_enable = true,
            .app_bin_check = false,
        },
    };
    esp_err_t ret = mmap_assets_new(&mmap_cfg, &s_assets);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "assets 分区不可用，跳过字体盘挂载 (%s)", esp_err_to_name(ret));
        return ret;
    }

    const fs_cfg_t fs_cfg = {
        .fs_letter = 'F',
        .fs_assets = s_assets,
        .fs_nums = 1,
    };
    ret = esp_lv_adapter_fs_mount(&fs_cfg, &s_fs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "挂载字体盘 F: 失败 (%s)", esp_err_to_name(ret));

        mmap_assets_del(s_assets);
        s_assets = NULL;
        return ret;
    }
#endif
    return ESP_OK;
}

/**
 * @brief 卸载 F: 盘并回收 mmap 句柄，与 mount_assets_fs() 配对。
 */
static void unmount_assets_fs(void)
{
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS
    if (s_fs_handle != NULL) {
        esp_lv_adapter_fs_unmount(s_fs_handle);
        s_fs_handle = NULL;
    }
    if (s_assets != NULL) {
        mmap_assets_del(s_assets);
        s_assets = NULL;
    }
#endif
}

esp_err_t music_player_display_init(void)
{
    ESP_RETURN_ON_FALSE(!s_display_inited, ESP_ERR_INVALID_STATE, TAG, "显示通路已经初始化过");

    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD), TAG,
                        "初始化 LCD 失败");
    s_lcd_board_inited = true;

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH), err_cleanup, TAG,
                      "初始化触摸失败");
    s_touch_board_inited = true;

    dev_display_lcd_handles_t *lcd_handles = NULL;
    dev_display_lcd_config_t *lcd_cfg = NULL;
    dev_lcd_touch_handles_t *touch_handles = NULL;

    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handles);
    ESP_GOTO_ON_FALSE(ret == ESP_OK && lcd_handles != NULL && lcd_handles->panel_handle != NULL, ESP_FAIL,
                      err_cleanup, TAG, "取 LCD 句柄失败");
    ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
    ESP_GOTO_ON_FALSE(ret == ESP_OK && lcd_cfg != NULL, ESP_FAIL, err_cleanup, TAG, "取 LCD 配置失败");
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, (void **)&touch_handles);
    ESP_GOTO_ON_FALSE(ret == ESP_OK && touch_handles != NULL && touch_handles->touch_handle != NULL, ESP_FAIL,
                      err_cleanup, TAG, "取触摸句柄失败");

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = 0;
#ifdef CONFIG_SPIRAM
    adapter_cfg.stack_in_psram = true;
#endif
    ESP_GOTO_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), err_cleanup, TAG, "初始化 LVGL 适配层失败");
    s_adapter_inited = true;

    esp_lv_adapter_display_config_t disp_cfg;
    if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                              lcd_handles->io_handle,
                                                              lcd_cfg->lcd_width,
                                                              lcd_cfg->lcd_height,
                                                              ESP_LV_ADAPTER_ROTATE_0);
        disp_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
#ifdef CONFIG_SPIRAM
        disp_cfg.profile.use_psram = true;
#endif
    } else if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB) == 0 ||
               strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0) {
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                             lcd_handles->io_handle,
                                                             lcd_cfg->lcd_width,
                                                             lcd_cfg->lcd_height,
                                                             ESP_LV_ADAPTER_ROTATE_0);
        disp_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
#ifdef CONFIG_SPIRAM
        disp_cfg.profile.use_psram = true;
#endif
    } else {

        disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                                        lcd_handles->io_handle,
                                                                        lcd_cfg->lcd_width,
                                                                        lcd_cfg->lcd_height,
                                                                        ESP_LV_ADAPTER_ROTATE_0);
    }

    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    ESP_GOTO_ON_FALSE(s_disp != NULL, ESP_FAIL, err_cleanup, TAG, "注册 LVGL 显示失败");

    esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, touch_handles->touch_handle);
    s_touch = esp_lv_adapter_register_touch(&touch_cfg);
    ESP_GOTO_ON_FALSE(s_touch != NULL, ESP_FAIL, err_cleanup, TAG, "注册触摸输入失败");
    s_touch_registered = true;

    mount_assets_fs();

    s_display_inited = true;
    return ESP_OK;

err_cleanup:
    display_release_adapter();
    display_release_board_devices();
    return ret;
}

esp_err_t music_player_display_start(void)
{
    ESP_RETURN_ON_FALSE(s_display_inited, ESP_ERR_INVALID_STATE, TAG, "显示通路尚未初始化");

    if (s_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "启动 LVGL 适配层失败");
    s_started = true;
    return ESP_OK;
}

void music_player_display_deinit(void)
{

    if (!s_display_inited && !s_started && !s_adapter_inited && !s_lcd_board_inited) {
        return;
    }

    display_release_adapter();
    display_release_board_devices();
    s_display_inited = false;
}
