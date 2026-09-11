/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"

#define MUSIC_PLAYER_QUEUE_LEN           16

#define MUSIC_PLAYER_TITLE_MAX           CONFIG_ESP_PLAYLIST_MEDIA_NAME_MAX

#define MUSIC_PLAYER_MODE_TEXT_MAX       16

#define MUSIC_PLAYER_SCAN_DEPTH          1

#define MUSIC_PLAYER_DEFAULT_TRACK_NAME  "hello_1.wav"

#define MUSIC_PLAYER_DEFAULT_VOLUME      10
#define MUSIC_PLAYER_VOLUME_MIN          0
#define MUSIC_PLAYER_VOLUME_MAX          100
#define MUSIC_PLAYER_VOLUME_STEP         5

#define MUSIC_PLAYER_EVENT_TASK_STACK    6144
#define MUSIC_PLAYER_EVENT_TASK_PRIO     5

#define MUSIC_PLAYER_ASP_TASK_STACK      6144
#define MUSIC_PLAYER_ASP_TASK_PRIO       10

#define MUSIC_PLAYER_FONT_PATH           "F:font.ttf"
#define MUSIC_PLAYER_FONT_SIZE           28

#define MUSIC_PLAYER_PROGRESS_POLL_MS    500

#define MUSIC_PLAYER_TAG_APP       "mp.app"
#define MUSIC_PLAYER_TAG_BOARD     "mp.board"
#define MUSIC_PLAYER_TAG_DISPLAY   "mp.display"
#define MUSIC_PLAYER_TAG_UI        "mp.ui"
#define MUSIC_PLAYER_TAG_PLAYLIST  "mp.playlist"
#define MUSIC_PLAYER_TAG_PLAYER    "mp.player"
#define MUSIC_PLAYER_TAG_BUTTON    "mp.button"
