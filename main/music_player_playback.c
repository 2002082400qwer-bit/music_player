/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "music_player_playback.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/lock.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_audio_es_extractor.h"
#include "esp_audio_simple_player.h"
#include "esp_extractor.h"
#include "esp_gmf_err.h"
#include "esp_wav_extractor.h"
#include "app.h"
#include "music_player_playlist.h"

#define MUSIC_PLAYER_EVT_REFRESH_STATE  (0x100)

#define MUSIC_PLAYER_PROBE_TIMEOUT_US  (1500 * 1000)

static const char *TAG = MUSIC_PLAYER_TAG_PLAYER;

typedef struct {
    FILE    *fp;
    int64_t  deadline_us;
} extractor_io_t;

typedef struct {
    _lock_t   lock;
    int       duration_ms;
    int       elapsed_acc_ms;
    int64_t   elapsed_base_us;
    bool      elapsed_running;
    uint32_t  track_gen;
} music_player_progress_t;

static esp_asp_handle_t s_player = NULL;
static esp_codec_dev_handle_t s_codec = NULL;
static bool s_extractors_ready = false;
static bool s_playing = false;

static bool s_pipeline_ready = false;
static music_player_playlist_mode_t s_mode = MUSIC_PLAYER_MODE_REPEAT_ALL;
static music_player_progress_t s_progress = {0};
static int s_invalid_tracks = 0;

/**
 * @brief  把音量限制在允许区间内，防止越界写进 codec。
 *
 * @param[in]  volume  待限制的音量值
 *
 * @return 夹在 [MUSIC_PLAYER_VOLUME_MIN, MUSIC_PLAYER_VOLUME_MAX] 之间的音量
 */
static inline int clamp_volume(int volume)
{
    if (volume < MUSIC_PLAYER_VOLUME_MIN) {
        return MUSIC_PLAYER_VOLUME_MIN;
    }
    return (volume > MUSIC_PLAYER_VOLUME_MAX) ? MUSIC_PLAYER_VOLUME_MAX : volume;
}

/**
 * @brief  读取 codec 当前音量；读不到时退回默认值，保证界面永远有合理显示。
 *
 * @return 当前音量（0~100）
 */
static int get_playback_volume_or_default(void)
{
    int volume = MUSIC_PLAYER_DEFAULT_VOLUME;
    if (s_codec != NULL && esp_codec_dev_get_out_vol(s_codec, &volume) == ESP_OK) {
        return clamp_volume(volume);
    }
    return MUSIC_PLAYER_DEFAULT_VOLUME;
}

/**
 * @brief  把播放列表里的媒体 URL 转成播放器能接受的本地路径。
 *
 * 播放列表里存的是 "file:///sdcard/xxx.mp3" 这类 URL，
 * 而 esp_audio_simple_player 需要本地路径，所以要剥掉 "file:" 前缀和多余斜杠。
 *
 * @param[in]   url       播放列表里的媒体 URL
 * @param[out]  out       输出缓冲区
 * @param[in]   out_size  输出缓冲区字节数
 */
static void playlist_url_to_player_uri(const char *url, char *out, size_t out_size)
{
    if (url == NULL || out == NULL || out_size == 0) {
        return;
    }
    const char *path = url;
    if (strncmp(url, "file:", 5) == 0) {
        path = url + 5;
        while (path[0] == '/' && path[1] == '/') {
            path++;
        }
    }
    snprintf(out, out_size, "%s", path);
}

/**
 * @brief  结算并暂停计时：把"本段已播放的时间"累加进累计值。
 *
 * 暂停、切歌、停止时都要调用，否则这段播放时间会被丢掉或重复计算。
 */
static void progress_elapsed_pause(void)
{
    _lock_acquire(&s_progress.lock);
    if (s_progress.elapsed_running) {
        int64_t now = esp_timer_get_time();
        s_progress.elapsed_acc_ms += (int)((now - s_progress.elapsed_base_us) / 1000);
        if (s_progress.elapsed_acc_ms < 0) {
            s_progress.elapsed_acc_ms = 0;
        }
        s_progress.elapsed_running = false;
    }
    _lock_release(&s_progress.lock);
}

/**
 * @brief  重新开始计时：记下当前时刻作为本段起点。恢复播放时调用。
 */
static void progress_elapsed_resume(void)
{
    _lock_acquire(&s_progress.lock);
    s_progress.elapsed_base_us = esp_timer_get_time();
    s_progress.elapsed_running = true;
    _lock_release(&s_progress.lock);
}

/**
 * @brief  计算当前已播放毫秒数（调用者必须已持有 s_progress.lock）。
 *
 * 结果会被夹在 [0, duration_ms] 之间，避免界面出现负进度或超过总长。
 *
 * @return 已播放毫秒数
 */
static int progress_get_elapsed_ms_unlocked(void)
{
    int elapsed = s_progress.elapsed_acc_ms;
    if (s_progress.elapsed_running) {
        elapsed += (int)((esp_timer_get_time() - s_progress.elapsed_base_us) / 1000);
    }
    if (elapsed < 0) {
        elapsed = 0;
    }
    if (s_progress.duration_ms > 0 && elapsed > s_progress.duration_ms) {
        elapsed = s_progress.duration_ms;
    }
    return elapsed;
}

/**
 * @brief  注册音频解封装器。
 *
 * mp3/aac 这类走 ES/TS 解封装器，wav 走专用 wav 解封装器，
 * 两个都成功才算就绪；第二个失败会把第一个回滚掉，避免留下半注册状态。
 *
 * @return ESP_OK 表示两个解封装器都已注册
 */
static esp_err_t register_extractors(void)
{
    if (s_extractors_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(esp_audio_es_extractor_register() == ESP_EXTRACTOR_ERR_OK, ESP_FAIL, TAG,
                        "注册音频解封装器失败");
    ESP_RETURN_ON_FALSE(esp_wav_extractor_register() == ESP_EXTRACTOR_ERR_OK, ESP_FAIL, TAG,
                        "注册 WAV 解封装器失败");
    s_extractors_ready = true;
    return ESP_OK;
}

/**
 * @brief  反注册解封装器，与 register_extractors() 配对。
 */
static void unregister_extractors(void)
{
    if (!s_extractors_ready) {
        return;
    }
    esp_wav_extractor_unregister();
    esp_audio_es_extractor_unregister();
    s_extractors_ready = false;
}

/**
 * @brief  解封装器的读回调：超时则返回 -1 让上层放弃探测。
 *
 * @param[out]  buffer  读到的数据写入这里
 * @param[in]   size    期望读取的字节数
 * @param[in]   ctx     指向 extractor_io_t 的上下文
 *
 * @return 实际读到的字节数，超时或失败时为 -1
 */
static int extractor_read(void *buffer, uint32_t size, void *ctx)
{
    extractor_io_t *io = (extractor_io_t *)ctx;
    return esp_timer_get_time() >= io->deadline_us ? -1 : (int)fread(buffer, 1, size, io->fp);
}

/**
 * @brief  解封装器的定位回调，同样带超时保护。
 *
 * @param[in]  position  目标偏移（字节）
 * @param[in]  ctx       指向 extractor_io_t 的上下文
 *
 * @return 0 表示成功，超时或失败时为 -1
 */
static int extractor_seek(uint32_t position, void *ctx)
{
    extractor_io_t *io = (extractor_io_t *)ctx;
    return esp_timer_get_time() >= io->deadline_us ? -1 : fseek(io->fp, position, SEEK_SET);
}

/**
 * @brief  解封装器查询文件大小的回调。
 *
 * 实现方式是"记住当前位置 -> 跳到文件尾读大小 -> 跳回原位置"，
 * 不能直接改掉文件指针，否则会破坏后续解封装。
 *
 * @param[in]  ctx  指向 extractor_io_t 的上下文
 *
 * @return 文件字节数，超时或失败时为 0
 */
static uint32_t extractor_size(void *ctx)
{
    extractor_io_t *io = (extractor_io_t *)ctx;
    if (esp_timer_get_time() >= io->deadline_us) {
        return 0;
    }
    FILE *fp = io->fp;
    long current = ftell(fp);
    if (current < 0 || fseek(fp, 0, SEEK_END) != 0) {
        return 0;
    }
    long size = ftell(fp);
    fseek(fp, current, SEEK_SET);
    return (size > 0) ? (uint32_t)size : 0;
}

/**
 * @brief  探测音频文件的时长（毫秒）。
 *
 * 流程：打开文件 -> 用 esp_extractor 解出音频流信息 -> 取出 duration 字段。
 * 这是一次独立探测，与真正播放时的解码是两套流程。
 *
 * @param[in]  path  音频文件路径
 *
 * @return 时长毫秒数；解封装器未就绪、文件打不开或探测失败时返回 -1
 */
static int get_duration_ms(const char *path)
{
    if (!s_extractors_ready) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    int duration = -1;
    extractor_io_t io = {
        .fp = fp,
        .deadline_us = esp_timer_get_time() + MUSIC_PLAYER_PROBE_TIMEOUT_US,
    };
    esp_extractor_handle_t extractor = NULL;
    esp_extractor_config_t cfg = {
        .extract_mask = ESP_EXTRACT_MASK_AUDIO,
        .in_read_cb = extractor_read,
        .in_seek_cb = extractor_seek,
        .in_size_cb = extractor_size,
        .in_ctx = &io,
    };
    if (esp_extractor_open(&cfg, &extractor) == ESP_EXTRACTOR_ERR_OK &&
        esp_extractor_parse_stream(extractor) == ESP_EXTRACTOR_ERR_OK) {
        esp_extractor_stream_info_t info = {0};
        if (esp_extractor_get_stream_info(extractor, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, &info) ==
            ESP_EXTRACTOR_ERR_OK) {
            duration = info.duration;
        }
    }
    if (extractor != NULL) {
        esp_extractor_close(extractor);
    }
    fclose(fp);
    return duration;
}

/**
 * @brief  开始一首新歌时重置进度：设定总时长，清空累计值并重新开始计时。
 *
 * @param[in]  duration_ms  曲目总时长；传 0 表示时长未知，此时界面只显示已播放时间
 */
static void reset_progress_state(int duration_ms)
{
    _lock_acquire(&s_progress.lock);
    s_progress.duration_ms = (duration_ms > 0) ? duration_ms : 0;
    s_progress.elapsed_acc_ms = 0;
    s_progress.elapsed_base_us = esp_timer_get_time();
    s_progress.elapsed_running = true;
    _lock_release(&s_progress.lock);
}

/**
 * @brief  播放器的数据出口：把解码出的 PCM 直接写进音频 codec。
 *
 * 返回值必须是"实际写出的字节数"，播放器据此判断是否需要等一等重试；
 * 写失败时返回 0 并记一条 debug 日志（重复失败时不至于刷屏）。
 *
 * @param[in]  data       解码出来的 PCM 数据
 * @param[in]  data_size  数据字节数
 * @param[in]  ctx        音频 codec 句柄
 *
 * @return 实际写出的字节数，失败时为 0
 */
static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    esp_codec_dev_handle_t codec = (esp_codec_dev_handle_t)ctx;
    if (codec == NULL || data == NULL || data_size <= 0) {
        return 0;
    }
    int ret = esp_codec_dev_write(codec, data, data_size);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGD(TAG, "写 PCM 失败: ret=%d, size=%d", ret, data_size);
        return 0;
    }
    return data_size;
}

/**
 * @brief  把当前播放状态打包成快照发给界面。
 *
 * 只在 mp_evt 任务里调用，因此读曲库、读 codec 都是安全的。
 */
static void publish_state(void)
{
    music_player_state_t state = {0};
    state.volume = get_playback_volume_or_default();
    state.playing = s_playing;
    snprintf(state.mode_text, sizeof(state.mode_text), "%s", music_player_playlist_mode_text(s_mode));
    if (music_player_playlist_current(NULL, state.title, sizeof(state.title)) != ESP_OK) {
        snprintf(state.title, sizeof(state.title), "%s", "未找到音乐");
    }
    app_event_post(MUSIC_PLAYER_EVT_STATE_CHANGED, &state, sizeof(state));
}

/**
 * @brief  播放器事件回调：把"状态变化"翻译成事件投递回事件任务。
 *
 * 这个回调运行在播放器自己的任务上下文里，所以绝不能在里面对播放器做操作，
 * 只能投递事件让事件任务去处理。
 *   - FINISHED / ERROR：表示一首放完或出错，连同当前 track_gen 一起投递，
 *     事件任务稍后会核对世代号，避免处理过期事件；
 *   - RUNNING / PAUSED / STOPPED：只是状态变化，请事件任务重新发布一次状态快照。
 *
 * @param[in]  event  播放器上报的事件包
 * @param[in]  ctx    注册时传入的用户上下文，本文件未使用
 *
 * @return 固定返回 0，表示回调已处理
 */
static int player_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    (void)ctx;
    if (event == NULL || event->payload == NULL) {
        return 0;
    }
    if (event->type != ESP_ASP_EVENT_TYPE_STATE || event->payload_size < sizeof(esp_asp_state_t)) {
        return 0;
    }

    esp_asp_state_t state = ESP_ASP_STATE_NONE;
    memcpy(&state, event->payload, sizeof(state));

    if (state == ESP_ASP_STATE_FINISHED || state == ESP_ASP_STATE_ERROR) {
        uint32_t gen = 0;
        _lock_acquire(&s_progress.lock);
        gen = s_progress.track_gen;
        _lock_release(&s_progress.lock);
        const int32_t evt = (state == ESP_ASP_STATE_FINISHED) ? MUSIC_PLAYER_EVT_TRACK_FINISHED
                                                              : MUSIC_PLAYER_EVT_TRACK_ERROR;
        app_event_post(evt, &gen, sizeof(gen));
    } else if (state == ESP_ASP_STATE_RUNNING || state == ESP_ASP_STATE_PAUSED ||
               state == ESP_ASP_STATE_STOPPED) {
        app_event_post(MUSIC_PLAYER_EVT_REFRESH_STATE, NULL, 0);
    }
    return 0;
}

/**
 * @brief  设置音量并同步给界面。
 *
 * @param[in]  volume  目标音量，超范围时会被夹到合法区间
 *
 * @return ESP_OK 或 codec 返回的错误码
 */
static esp_err_t set_playback_volume(int volume)
{
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_ERR_INVALID_STATE, TAG, "codec 尚未就绪");
    int new_volume = clamp_volume(volume);
    esp_err_t ret = esp_codec_dev_set_out_vol(s_codec, new_volume);
    ESP_RETURN_ON_ERROR(ret, TAG, "设置音量失败: %d%%", new_volume);
    publish_state();
    return ESP_OK;
}

/**
 * @brief  播放曲库中"当前指向"的那首歌，是本文件最核心的一段流程。
 *
 * 步骤依次是：
 *   1. 取当前曲目 URL，转成播放器能识别的本地路径；
 *   2. 先停掉上一首，并把 track_gen 加一（此后旧事件一律作废）；
 *   3. 探测时长；探测失败说明文件无法解码，直接投递错误事件跳过这首歌；
 *   4. 重置进度状态，启动播放器；
 *   5. 更新 s_playing 并把新状态发给界面。
 *
 * @return
 *       - ESP_OK                    开始播放成功
 *       - ESP_ERR_INVALID_STATE     播放器/曲库尚未就绪
 *       - ESP_ERR_INVALID_RESPONSE  文件无法解码，已请求跳过
 *       - ESP_FAIL                  播放器启动失败
 */
static esp_err_t play_current_track(void)
{
    ESP_RETURN_ON_FALSE(s_player != NULL, ESP_ERR_INVALID_STATE, TAG, "播放器尚未创建");

    char url[CONFIG_ESP_PLAYLIST_URL_MAX] = {0};
    esp_err_t ret = music_player_playlist_current_url(url, sizeof(url));
    if (ret != ESP_OK) {
        s_playing = false;
        publish_state();
        return ret;
    }

    char uri[CONFIG_ESP_PLAYLIST_URL_MAX] = {0};
    playlist_url_to_player_uri(url, uri, sizeof(uri));

    if (s_pipeline_ready) {
        esp_audio_simple_player_stop(s_player);
    }
    _lock_acquire(&s_progress.lock);
    s_progress.track_gen++;
    uint32_t track_gen = s_progress.track_gen;
    _lock_release(&s_progress.lock);

    int duration_ms = get_duration_ms(uri);
    if (duration_ms < 0) {
        reset_progress_state(0);
        s_playing = false;
        ESP_LOGW(TAG, "跳过无法解码的文件: %s", uri);
        app_event_post(MUSIC_PLAYER_EVT_TRACK_ERROR, &track_gen, sizeof(track_gen));
        return ESP_ERR_INVALID_RESPONSE;
    }
    reset_progress_state(duration_ms);

    esp_gmf_err_t err = esp_audio_simple_player_run(s_player, uri, NULL);
    if (err != ESP_GMF_ERR_OK) {
        s_playing = false;
        progress_elapsed_pause();
        publish_state();
        ESP_LOGE(TAG, "启动播放失败: %d，文件 %s", (int)err, uri);
        return ESP_FAIL;
    }

    s_pipeline_ready = true;
    s_playing = true;
    publish_state();
    return ESP_OK;
}

/**
 * @brief  从当前曲目开始播放，并重置"连续坏文件"计数。
 *
 * @return play_current_track() 的结果
 */
static esp_err_t start_track_playback(void)
{
    s_invalid_tracks = 0;
    return play_current_track();
}

/**
 * @brief  上一首 / 下一首。
 *
 * @param[in]  next  true 表示下一首，false 表示上一首
 *
 * @return 曲库移动指针的结果，或随后的播放结果
 */
static esp_err_t navigate_track(bool next)
{
    s_invalid_tracks = 0;
    esp_err_t ret = music_player_playlist_step(next);
    if (ret != ESP_OK) {
        return ret;
    }
    return play_current_track();
}

/**
 * @brief  点播曲库中指定序号的曲目（用户点了列表中的某一项）。
 *
 * @param[in]  index  曲目在播放列表中的序号
 *
 * @return 曲库设置当前序号的结果，或随后的播放结果
 */
static esp_err_t play_track_by_index(int index)
{
    s_invalid_tracks = 0;
    esp_err_t ret = music_player_playlist_set_index(index);
    ESP_RETURN_ON_ERROR(ret, TAG, "切换到第 %d 首失败", index);
    return play_current_track();
}

/**
 * @brief  暂停当前曲目：停住声音但保留播放位置，之后收到切换事件可从暂停点继续。
 */
static void pause_current_track(void)
{
    if (s_player == NULL) {
        return;
    }
    esp_audio_simple_player_pause(s_player);
    progress_elapsed_pause();
    s_playing = false;
    publish_state();
}

/**
 * @brief  继续播放：播放器处于暂停态就从暂停点继续，否则从头播放当前曲目。
 *
 * 上电后第一次按播放键时还没有"暂停点"（内部管线尚未创建），
 * 此时直接走 start_track_playback() 从头播放；暂停过再按才会真的调用 resume()。
 */
static void resume_or_start_playback(void)
{

    if (s_pipeline_ready && s_player != NULL && esp_audio_simple_player_resume(s_player) == ESP_GMF_ERR_OK) {
        progress_elapsed_resume();
        s_playing = true;
        publish_state();
    } else {
        start_track_playback();
    }
}

/**
 * @brief  处理"一首播放结束"或"一首播放出错"。
 *
 * 开头先做世代号校验，丢弃切歌后迟到的旧事件。
 * 出错时累计 s_invalid_tracks，连续失败次数达到曲目总数就认为整卡没有可播放内容，
 * 停下并同步界面，避免无限循环重试。
 *
 * 中间那段 REPEAT_ONE 临时切到 REPEAT_ALL 的处理，是为了绕开一个死循环：
 * 单曲循环模式下如果这首歌本身是坏文件，重试永远还是同一首，
 * 所以出错的这一次先按列表循环前进到下一首，处理完再切回单曲循环。
 *
 * @param[in]  is_error    true 表示播放出错，false 表示正常放完
 * @param[in]  event_gen   事件携带的曲目世代号
 */
static void handle_track_end_or_error(bool is_error, uint32_t event_gen)
{
    _lock_acquire(&s_progress.lock);
    uint32_t cur_gen = s_progress.track_gen;
    _lock_release(&s_progress.lock);
    if (event_gen != cur_gen) {
        ESP_LOGD(TAG, "丢弃过期的%s事件: gen=%" PRIu32 " current=%" PRIu32,
                 is_error ? "错误" : "结束", event_gen, cur_gen);
        return;
    }

    if (is_error) {
        int count = music_player_playlist_count();
        if (count > 0 && ++s_invalid_tracks >= count) {
            s_playing = false;
            progress_elapsed_pause();
            ESP_LOGE(TAG, "整张卡都找不到能播放的音频");
            publish_state();
            return;
        }
    } else {
        s_invalid_tracks = 0;
    }

    if (is_error && s_mode == MUSIC_PLAYER_MODE_REPEAT_ONE) {
        music_player_playlist_set_mode(MUSIC_PLAYER_MODE_REPEAT_ALL);
    }
    if (music_player_playlist_step(true) == ESP_OK) {
        play_current_track();
    } else {
        s_playing = false;
        progress_elapsed_pause();
        publish_state();
    }
    if (is_error && s_mode == MUSIC_PLAYER_MODE_REPEAT_ONE) {
        music_player_playlist_set_mode(MUSIC_PLAYER_MODE_REPEAT_ONE);
    }
}

/**
 * @brief  播放引擎的事件入口：所有控制意图都在这里被翻译成具体动作。
 *
 * 回调运行在 mp_evt 任务里，因此这里是全工程唯一操作播放器的地方，天然串行。
 * 事件语义与数据格式见 app.h。
 *
 * @param[in]  arg   订阅时传入的用户上下文，本文件未使用
 * @param[in]  base  事件基，固定为 MUSIC_PLAYER_EVENT
 * @param[in]  id    事件 ID
 * @param[in]  data  事件数据，可能为 NULL
 */
static void playback_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id) {
        case MUSIC_PLAYER_EVT_REFRESH_STATE:
            publish_state();
            return;

        case MUSIC_PLAYER_EVT_VOLUME_UP:
            set_playback_volume(get_playback_volume_or_default() + MUSIC_PLAYER_VOLUME_STEP);
            return;
        case MUSIC_PLAYER_EVT_VOLUME_DOWN:
            set_playback_volume(get_playback_volume_or_default() - MUSIC_PLAYER_VOLUME_STEP);
            return;
        case MUSIC_PLAYER_EVT_TRACK_FINISHED:
        case MUSIC_PLAYER_EVT_TRACK_ERROR: {
            uint32_t gen = (data != NULL) ? *(const uint32_t *)data : 0;
            handle_track_end_or_error(id == MUSIC_PLAYER_EVT_TRACK_ERROR, gen);
            return;
        }
        default:
            break;
    }

    if (!music_player_playlist_ready()) {
        return;
    }

    switch (id) {
        case MUSIC_PLAYER_EVT_TOGGLE:

            if (s_playing) {
                pause_current_track();
            } else {
                resume_or_start_playback();
            }
            break;
        case MUSIC_PLAYER_EVT_NEXT:
            navigate_track(true);
            break;
        case MUSIC_PLAYER_EVT_PREV:
            navigate_track(false);
            break;
        case MUSIC_PLAYER_EVT_PLAY_INDEX:
            if (data != NULL) {
                play_track_by_index(*(const int *)data);
            }
            break;
        case MUSIC_PLAYER_EVT_TOGGLE_MODE:
            s_mode = (music_player_playlist_mode_t)((s_mode + 1) % MUSIC_PLAYER_MODE_MAX);
            music_player_playlist_set_mode(s_mode);
            publish_state();
            break;
        default:
            break;
    }
}

esp_err_t music_player_playback_init(esp_codec_dev_handle_t codec)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(codec != NULL, ESP_ERR_INVALID_ARG, TAG, "codec 句柄为空");
    ESP_RETURN_ON_FALSE(s_player == NULL, ESP_ERR_INVALID_STATE, TAG, "播放引擎已经初始化");

    s_codec = codec;
    ESP_GOTO_ON_ERROR(register_extractors(), err_cleanup, TAG, "注册解封装器失败");

    esp_asp_cfg_t cfg = {
        .out.cb = out_data_callback,
        .out.user_ctx = s_codec,
        .task_prio = MUSIC_PLAYER_ASP_TASK_PRIO,
        .task_stack = MUSIC_PLAYER_ASP_TASK_STACK,
    };
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &s_player);
    ESP_GOTO_ON_FALSE(err == ESP_GMF_ERR_OK && s_player != NULL, ESP_FAIL, err_cleanup, TAG,
                      "创建播放器失败: %d", (int)err);

    err = esp_audio_simple_player_set_event(s_player, player_event_callback, NULL);
    ESP_GOTO_ON_FALSE(err == ESP_GMF_ERR_OK, ESP_FAIL, err_cleanup, TAG, "注册播放器事件回调失败: %d", (int)err);

    ESP_GOTO_ON_ERROR(app_event_subscribe(ESP_EVENT_ANY_ID, playback_event_handler, NULL), err_cleanup, TAG,
                      "订阅播放事件失败");

    publish_state();
    return ESP_OK;

err_cleanup:
    music_player_playback_deinit();
    return ret;
}

void music_player_playback_deinit(void)
{

    app_event_unsubscribe(ESP_EVENT_ANY_ID, playback_event_handler);

    if (s_player != NULL) {
        if (s_pipeline_ready) {
            esp_audio_simple_player_stop(s_player);
        }
        esp_audio_simple_player_destroy(s_player);
        s_player = NULL;
    }
    unregister_extractors();

    s_codec = NULL;
    s_playing = false;
    s_pipeline_ready = false;
    s_invalid_tracks = 0;
    s_mode = MUSIC_PLAYER_MODE_REPEAT_ALL;
    _lock_acquire(&s_progress.lock);
    s_progress.duration_ms = 0;
    s_progress.elapsed_acc_ms = 0;
    s_progress.elapsed_running = false;
    _lock_release(&s_progress.lock);
}

esp_err_t music_player_playback_get_progress(int *elapsed_ms, int *duration_ms)
{
    ESP_RETURN_ON_FALSE(elapsed_ms != NULL && duration_ms != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "进度缓冲区无效");

    _lock_acquire(&s_progress.lock);
    *elapsed_ms = progress_get_elapsed_ms_unlocked();
    *duration_ms = s_progress.duration_ms;
    _lock_release(&s_progress.lock);
    return ESP_OK;
}
