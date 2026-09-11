/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "music_player_playlist.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_playlist.h"

static const char *TAG = MUSIC_PLAYER_TAG_PLAYLIST;

static esp_media_db_handle_t s_media_db = NULL;
static esp_playlist_handle_t s_playlist = NULL;

static const char *s_mode_text[MUSIC_PLAYER_MODE_MAX] = {
    "单曲循环",
    "列表循环",
    "随机播放",
};

/**
 * @brief  把本模块的循环模式翻译成播放列表组件认识的循环模式。
 *
 * @param[in]  mode  本模块的循环模式
 *
 * @return esp_playlist 的循环模式；取值越界时按随机播放处理
 */
static esp_playlist_repeat_mode_t mode_to_playlist(music_player_playlist_mode_t mode)
{
    switch (mode) {
        case MUSIC_PLAYER_MODE_REPEAT_ONE:
            return ESP_PLAYLIST_REPEAT_ONE;
        case MUSIC_PLAYER_MODE_REPEAT_ALL:
            return ESP_PLAYLIST_REPEAT_ALL;
        case MUSIC_PLAYER_MODE_SHUFFLE:
        default:
            return ESP_PLAYLIST_REPEAT_SHUFFLE;
    }
}

/**
 * @brief  比较两个文件名是否相同（忽略大小写）。
 *
 * FAT 文件名不区分大小写，卡上可能存成 HELLO_1.WAV，因此不能直接用 strcmp。
 *
 * @param[in]  a  待比较的文件名
 * @param[in]  b  待比较的文件名
 *
 * @return true 表示两个名字逐字符相同（忽略大小写）
 */
static bool name_matches_ignore_case(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @brief  取 URL 里最后一个 '/' 之后的部分。
 *
 * 播放列表存的是 "file:///sdcard/hello.wav" 这类 URL，
 * 只比较文件名时需要先把目录部分剥掉。
 *
 * @param[in]  url  播放列表里的媒体 URL
 *
 * @return 指向文件名部分的指针；url 为 NULL 时返回 NULL
 */
static const char *url_basename(const char *url)
{
    if (url == NULL) {
        return NULL;
    }
    const char *slash = strrchr(url, '/');
    return (slash != NULL) ? (slash + 1) : url;
}

/**
 * @brief  把播放列表的当前指针指到指定文件名的曲目（开机默认曲目）。
 *
 * 扫描导入后列表默认停在第 0 首，本函数按 MUSIC_PLAYER_DEFAULT_TRACK_NAME
 * 找到对应下标并设为当前曲目，这样用户第一次按 PLAY 播放的就是这首，
 * 界面也直接显示它的名字。找不到时保持第 0 首不动，只打一条警告，不算错误。
 *
 * @param[in]  name  期望选中的文件名，例如 "hello_1.wav"
 */
static void select_default_track(const char *name)
{
    if (s_playlist == NULL || name == NULL || name[0] == '\0') {
        return;
    }

    int count = 0;
    if (esp_playlist_get_count(s_playlist, &count) != ESP_OK) {
        return;
    }

    for (int i = 0; i < count; i++) {
        esp_playlist_info_t info = {0};
        if (esp_playlist_get_info(s_playlist, i, &info) != ESP_OK) {
            continue;
        }
        if (name_matches_ignore_case(info.media_name, name) ||
            name_matches_ignore_case(url_basename(info.media_url), name)) {
            esp_playlist_set_curr_index(s_playlist, i);
            return;
        }
    }
    ESP_LOGW(TAG, "默认曲目 %s 不在卡上，保持列表第一首", name);
}

esp_err_t music_player_playlist_scan(const char *scan_dir)
{
    ESP_RETURN_ON_FALSE(scan_dir != NULL, ESP_ERR_INVALID_ARG, TAG, "扫描目录为空");
    ESP_RETURN_ON_FALSE(s_media_db == NULL && s_playlist == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "曲库已经建立过，请先调用 music_player_playlist_deinit()");

    const esp_media_db_cfg_t db_cfg = {
        .storage_type = ESP_DB_STORAGE_RAM,
        .storage_path = "music_player_db",
    };
    esp_err_t ret = esp_media_db_init(&db_cfg, &s_media_db);
    ESP_RETURN_ON_ERROR(ret, TAG, "初始化媒体库失败");

    const char *exts[] = {".mp3", ".aac", ".wav"};
    const esp_media_db_scan_cfg_t scan_cfg = {
        .skip_duplicate = true,
        .path = scan_dir,
        .scan_depth = MUSIC_PLAYER_SCAN_DEPTH,
        .file_extensions = exts,
        .file_extension_count = 3,
    };
    ret = esp_media_db_scan(s_media_db, &scan_cfg);

    int count = 0;
    if (ret == ESP_OK) {
        esp_media_db_get_count(s_media_db, &count);
    }
    if (ret != ESP_OK || count <= 0) {
        music_player_playlist_deinit();
        return (ret != ESP_OK) ? ret : ESP_ERR_NOT_FOUND;
    }

    ret = esp_playlist_new(&(esp_playlist_cfg_t) {
                               .playlist_name = "music_player",
                           },
                           &s_playlist);
    if (ret != ESP_OK) {
        music_player_playlist_deinit();
        return ret;
    }

    ret = esp_playlist_import_media(s_playlist, s_media_db, NULL);
    if (ret != ESP_OK) {
        music_player_playlist_deinit();
        return ret;
    }

    select_default_track(MUSIC_PLAYER_DEFAULT_TRACK_NAME);
    esp_playlist_set_repeat_mode(s_playlist, mode_to_playlist(MUSIC_PLAYER_MODE_REPEAT_ALL));
    return ESP_OK;
}

void music_player_playlist_deinit(void)
{
    if (s_playlist != NULL) {
        esp_playlist_del(s_playlist);
        s_playlist = NULL;
    }
    if (s_media_db != NULL) {
        esp_media_db_deinit(s_media_db);
        s_media_db = NULL;
    }
}

bool music_player_playlist_ready(void)
{
    return s_playlist != NULL;
}

int music_player_playlist_count(void)
{
    int count = 0;
    if (s_playlist == NULL || esp_playlist_get_count(s_playlist, &count) != ESP_OK) {
        return 0;
    }
    return count;
}

esp_err_t music_player_playlist_current(int *index, char *title, size_t title_size)
{
    if (title != NULL && title_size > 0) {
        title[0] = '\0';
    }
    if (index != NULL) {
        *index = -1;
    }
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");

    esp_playlist_info_t info = {0};
    esp_err_t ret = esp_playlist_curr(s_playlist, &info);
    ESP_RETURN_ON_ERROR(ret, TAG, "读取当前曲目失败");

    if (index != NULL) {
        *index = info.index;
    }
    if (title != NULL && title_size > 0) {
        snprintf(title, title_size, "%s", info.media_name);
    }
    return ESP_OK;
}

esp_err_t music_player_playlist_title(int index, char *title, size_t title_size)
{
    ESP_RETURN_ON_FALSE(title != NULL && title_size > 0, ESP_ERR_INVALID_ARG, TAG, "曲目名缓冲区无效");
    title[0] = '\0';
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");

    esp_playlist_info_t info = {0};
    esp_err_t ret = esp_playlist_get_info(s_playlist, index, &info);
    if (ret != ESP_OK) {

        ESP_LOGD(TAG, "取第 %d 首曲目信息失败: %s", index, esp_err_to_name(ret));
        return ret;
    }
    snprintf(title, title_size, "%s", info.media_name);
    return ESP_OK;
}

esp_err_t music_player_playlist_current_url(char *url, size_t url_size)
{
    ESP_RETURN_ON_FALSE(url != NULL && url_size > 0, ESP_ERR_INVALID_ARG, TAG, "URL 缓冲区无效");
    url[0] = '\0';
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");

    esp_playlist_info_t info = {0};
    esp_err_t ret = esp_playlist_curr(s_playlist, &info);
    ESP_RETURN_ON_ERROR(ret, TAG, "读取当前曲目失败");

    snprintf(url, url_size, "%s", info.media_url);
    return ESP_OK;
}

esp_err_t music_player_playlist_set_index(int index)
{
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");
    return esp_playlist_set_curr_index(s_playlist, index);
}

esp_err_t music_player_playlist_step(bool next)
{
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");
    esp_playlist_info_t info = {0};
    return next ? esp_playlist_next(s_playlist, &info) : esp_playlist_prev(s_playlist, &info);
}

esp_err_t music_player_playlist_set_mode(music_player_playlist_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_playlist != NULL, ESP_ERR_INVALID_STATE, TAG, "曲库尚未建立");
    return esp_playlist_set_repeat_mode(s_playlist, mode_to_playlist(mode));
}

const char *music_player_playlist_mode_text(music_player_playlist_mode_t mode)
{
    if ((int)mode < 0 || (int)mode >= (int)MUSIC_PLAYER_MODE_MAX) {
        return s_mode_text[MUSIC_PLAYER_MODE_REPEAT_ALL];
    }
    return s_mode_text[mode];
}
