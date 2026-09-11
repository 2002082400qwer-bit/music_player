/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"
#include "music_player_config.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(MUSIC_PLAYER_EVENT);

typedef enum {

    MUSIC_PLAYER_EVT_TOGGLE = 0,
    MUSIC_PLAYER_EVT_NEXT,
    MUSIC_PLAYER_EVT_PREV,
    MUSIC_PLAYER_EVT_PLAY_INDEX,
    MUSIC_PLAYER_EVT_VOLUME_UP,
    MUSIC_PLAYER_EVT_VOLUME_DOWN,
    MUSIC_PLAYER_EVT_TOGGLE_MODE,

    MUSIC_PLAYER_EVT_STATE_CHANGED,
    MUSIC_PLAYER_EVT_TRACK_FINISHED,
    MUSIC_PLAYER_EVT_TRACK_ERROR,
} music_player_event_t;

typedef struct {
    char  title[MUSIC_PLAYER_TITLE_MAX];
    char  mode_text[MUSIC_PLAYER_MODE_TEXT_MAX];
    int   volume;
    bool  playing;
} music_player_state_t;

/**
 * @brief  往事件总线投递一条事件（非阻塞，队列满时丢弃并告警）。
 *
 * 事件数据会被事件循环整体拷贝，因此调用方传栈上变量即可。
 *
 * @param[in]  evt   Event id to post, see music_player_event_t
 * @param[in]  data  Event payload, or NULL when the event carries no data
 * @param[in]  size  Size of the payload in bytes, 0 when data is NULL
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the event loop is not ready yet
 *       - ESP_ERR_TIMEOUT        If the event queue is full
 */
esp_err_t app_event_post(int32_t evt, const void *data, size_t size);

/**
 * @brief  订阅某条事件，回调在事件任务上下文串行执行。
 *
 * @param[in]  evt_id       Event id to subscribe, or ESP_EVENT_ANY_ID for all events
 * @param[in]  handler      Callback invoked when the event is dispatched
 * @param[in]  handler_arg  User context passed back to the callback
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the event loop is not ready yet
 *       - Others                 Errors from esp_event_handler_register_with()
 */
esp_err_t app_event_subscribe(int32_t evt_id, esp_event_handler_t handler, void *handler_arg);

/**
 * @brief  取消订阅，参数必须与订阅时完全一致。
 *
 * @param[in]  evt_id   Event id used when subscribing
 * @param[in]  handler  Callback used when subscribing
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the event loop is not ready
 *       - Others                 Errors from esp_event_handler_unregister_with()
 */
esp_err_t app_event_unsubscribe(int32_t evt_id, esp_event_handler_t handler);

/**
 * @brief  按固定顺序初始化整个播放器应用。
 *
 * 顺序：事件循环 -> 板级外设 -> 显示 -> 界面 -> 曲库扫描 -> 播放引擎 -> 实体按键。
 * 其中曲库扫描与实体按键失败只告警（空卡或没有按键也要能用界面），
 * 其它步骤失败会按相反顺序回收已经初始化的部分，并打印具体错误码。
 */
void app_start(void);

/**
 * @brief  释放 app_start() 申请的全部资源，按初始化的逆序执行。
 */
void app_stop(void);

#ifdef __cplusplus
}
#endif
