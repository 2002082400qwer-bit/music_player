/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "hello_wav_provision.h"
#include "music_player_board.h"
#include "music_player_buttons.h"
#include "music_player_display.h"
#include "music_player_playback.h"
#include "music_player_playlist.h"
#include "music_player_ui.h"

ESP_EVENT_DEFINE_BASE(MUSIC_PLAYER_EVENT);

static const char *TAG = MUSIC_PLAYER_TAG_APP;

static esp_event_loop_handle_t s_event_loop = NULL;

static bool s_started = false;

static music_player_board_t s_board = {0};

/**
 * @brief  调整全局日志级别。
 *
 * 第三方组件（GMF、音频、LVGL 等）的日志统一压到 WARN，避免开机刷屏；
 * 本工程自己的几个标签保留 INFO，用来看开机结果。
 * 想临时看全量日志，把下面第一行的 ESP_LOG_WARN 改成 ESP_LOG_INFO 即可。
 */
static void app_log_level_init(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(MUSIC_PLAYER_TAG_APP, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_BOARD, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_DISPLAY, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_UI, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_PLAYLIST, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_PLAYER, ESP_LOG_INFO);
    esp_log_level_set(MUSIC_PLAYER_TAG_BUTTON, ESP_LOG_INFO);
}

/**
 * @brief  创建事件循环任务，重复调用时直接返回成功。
 *
 * 事件任务的栈与优先级沿用原来播放控制任务的配置：
 * 栈要够大是因为播放、探测时长等动作都在这个任务里执行；
 * 固定跑在 CPU1，把 CPU0 留给 LVGL 任务，两者互不抢占。
 *
 * @return
 *       - ESP_OK    事件循环创建成功（或此前已创建）
 *       - Others    esp_event_loop_create() 返回的错误码
 */
static esp_err_t app_event_loop_init(void)
{
    if (s_event_loop != NULL) {
        return ESP_OK;
    }
    const esp_event_loop_args_t loop_args = {
        .queue_size = MUSIC_PLAYER_QUEUE_LEN,
        .task_name = "mp_evt",
        .task_priority = MUSIC_PLAYER_EVENT_TASK_PRIO,
        .task_stack_size = MUSIC_PLAYER_EVENT_TASK_STACK,
        .task_core_id = 1,
    };
    return esp_event_loop_create(&loop_args, &s_event_loop);
}

/**
 * @brief  销毁事件循环，与 app_event_loop_init() 配对。
 */
static void app_event_loop_deinit(void)
{
    if (s_event_loop != NULL) {
        esp_event_loop_delete(s_event_loop);
        s_event_loop = NULL;
    }
}

esp_err_t app_event_post(int32_t evt, const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(s_event_loop != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "事件循环尚未就绪，事件 %d 被丢弃", (int)evt);

    ESP_RETURN_ON_FALSE(evt != ESP_EVENT_ANY_ID, ESP_ERR_INVALID_ARG, TAG,
                        "事件 ID -1 是保留值，不能用于投递");

    esp_err_t ret = esp_event_post_to(s_event_loop, MUSIC_PLAYER_EVENT, (int32_t)evt, data, size, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "事件 %d 投递失败: %s", (int)evt, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t app_event_subscribe(int32_t evt_id, esp_event_handler_t handler, void *handler_arg)
{
    ESP_RETURN_ON_FALSE(s_event_loop != NULL, ESP_ERR_INVALID_STATE, TAG, "事件循环尚未就绪，无法订阅事件");
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "事件回调不能为空");
    return esp_event_handler_register_with(s_event_loop, MUSIC_PLAYER_EVENT, evt_id, handler, handler_arg);
}

esp_err_t app_event_unsubscribe(int32_t evt_id, esp_event_handler_t handler)
{
    ESP_RETURN_ON_FALSE(s_event_loop != NULL, ESP_ERR_INVALID_STATE, TAG, "事件循环尚未就绪，无法取消订阅");
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "事件回调不能为空");
    return esp_event_handler_unregister_with(s_event_loop, MUSIC_PLAYER_EVENT, evt_id, handler);
}

void app_start(void)
{
    esp_err_t ret = ESP_OK;

    app_log_level_init();

    if (s_started) {
        ESP_LOGW(TAG, "应用已经启动过，忽略本次调用");
        return;
    }

    ESP_GOTO_ON_ERROR(app_event_loop_init(), cleanup_loop, TAG, "创建事件循环失败");

    ESP_GOTO_ON_ERROR(music_player_board_init(&s_board), cleanup_board, TAG, "初始化板级外设失败");

    ESP_GOTO_ON_ERROR(music_player_display_init(), cleanup_display, TAG, "初始化显示失败");
    ESP_GOTO_ON_ERROR(music_player_ui_init(), cleanup_ui, TAG, "初始化界面失败");

    int track_count = 0;
    if (music_player_playlist_scan(s_board.scan_dir) == ESP_OK) {
        track_count = music_player_playlist_count();
    } else {
        ESP_LOGW(TAG, "未在 %s 找到可播放音频", s_board.scan_dir);
    }

    ESP_GOTO_ON_ERROR(music_player_playback_init(s_board.playback_codec), cleanup_playlist, TAG, "初始化播放引擎失败");

    s_started = true;
    ESP_LOGI(TAG, "启动完成，曲目 %d 首，按 PLAY 键或屏幕播放键开始播放", track_count);
    return;

cleanup_playlist:
    music_player_playlist_deinit();
cleanup_ui:
    music_player_ui_deinit();
cleanup_display:
    music_player_display_deinit();
cleanup_board:
    music_player_board_deinit(&s_board);
cleanup_loop:
    app_event_loop_deinit();
    ESP_LOGE(TAG, "启动失败，已回滚全部已初始化资源，错误码: %s", esp_err_to_name(ret));
}

void app_stop(void)
{
    if (!s_started) {
        return;
    }
    s_started = false;

    music_player_buttons_deinit();
    music_player_playback_deinit();
    music_player_playlist_deinit();
    music_player_ui_deinit();
    music_player_display_deinit();
    music_player_board_deinit(&s_board);
    app_event_loop_deinit();
}
