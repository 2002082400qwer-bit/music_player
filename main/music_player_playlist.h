/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "music_player_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSIC_PLAYER_MODE_REPEAT_ONE = 0,
    MUSIC_PLAYER_MODE_REPEAT_ALL,
    MUSIC_PLAYER_MODE_SHUFFLE,
    MUSIC_PLAYER_MODE_MAX,
} music_player_playlist_mode_t;

/**
 * @brief  扫描指定目录并建立播放列表。
 *
 * 流程：初始化内存媒体库 -> 扫描 .mp3/.aac/.wav -> 建播放列表 -> 导入扫描结果 ->
 * 按 MUSIC_PLAYER_DEFAULT_TRACK_NAME 选中开机默认曲目。
 * 扫到 0 首时返回 ESP_ERR_NOT_FOUND，并把已经创建的对象回收干净。
 *
 * @param[in]  scan_dir  待扫描的目录，一般是 SD 卡挂载点，例如 "/sdcard"
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If scan_dir is NULL
 *       - ESP_ERR_INVALID_STATE If a playlist already exists
 *       - ESP_ERR_NOT_FOUND    If no supported music file is found
 *       - Others               Errors from media database or playlist operations
 */
esp_err_t music_player_playlist_scan(const char *scan_dir);

/**
 * @brief  释放播放列表与媒体库，可安全重复调用。
 */
void music_player_playlist_deinit(void);

/**
 * @brief  查询曲库是否可用（界面上"有没有歌可放"就看它）。
 *
 * @return true 表示已经扫描出至少一首曲目
 */
bool music_player_playlist_ready(void);

/**
 * @brief  取曲目总数。
 *
 * @return 曲目数量；曲库未就绪时返回 0
 */
int music_player_playlist_count(void);

/**
 * @brief  取当前曲目的序号与文件名。
 *
 * index 与 title 都可以传 NULL，表示只关心其中一项。
 *
 * @param[out]  index       当前曲目在列表中的序号，可为 NULL
 * @param[out]  title       曲目名输出缓冲区，可为 NULL
 * @param[in]   title_size  title 缓冲区的字节数
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 *       - Others                 Errors from playlist query
 */
esp_err_t music_player_playlist_current(int *index, char *title, size_t title_size);

/**
 * @brief  取当前曲目的媒体 URL（播放引擎据此换算成播放器能用的本地路径）。
 *
 * @param[out]  url       媒体 URL 输出缓冲区，例如 "file:///sdcard/hello.wav"
 * @param[in]   url_size  url 缓冲区的字节数
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If url is NULL or url_size is 0
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 *       - Others                 Errors from playlist query
 */
esp_err_t music_player_playlist_current_url(char *url, size_t url_size);

/**
 * @brief  按序号取曲目名，复制到调用者提供的缓冲区（供界面画列表）。
 *
 * @param[in]   index       Playlist index to query
 * @param[out]  title       Output buffer for the track title
 * @param[in]   title_size  Size of the output buffer in bytes
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If title is NULL or title_size is 0
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 *       - Others                 Errors from playlist query
 */
esp_err_t music_player_playlist_title(int index, char *title, size_t title_size);

/**
 * @brief  把当前曲目指针移到指定序号（界面点播某首时用）。
 *
 * @param[in]  index  目标序号，越界时由播放列表组件返回错误
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 *       - Others                 Errors from playlist operation
 */
esp_err_t music_player_playlist_set_index(int index);

/**
 * @brief  按当前循环模式前进或后退一首。
 *
 * @param[in]  next  true 表示下一首，false 表示上一首
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 *       - Others                 Errors from playlist operation
 */
esp_err_t music_player_playlist_step(bool next);

/**
 * @brief  设置播放顺序模式。
 *
 * @param[in]  mode  目标模式，见 music_player_playlist_mode_t
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If no playlist is available
 */
esp_err_t music_player_playlist_set_mode(music_player_playlist_mode_t mode);

/**
 * @brief  取循环模式对应的中文文案，用于界面显示。
 *
 * @param[in]  mode  循环模式；越界时按列表循环处理
 *
 * @return 指向静态字符串的指针，调用方不需要释放
 */
const char *music_player_playlist_mode_text(music_player_playlist_mode_t mode);

#ifdef __cplusplus
}
#endif
