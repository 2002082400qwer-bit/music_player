# SD Card Music Player

- [中文版本](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

This example demonstrates an SD card music player with a touch-screen UI. It scans local music files on a microSD card, shows the player screen, song information, and playback controls on the display, and plays music through the board audio output device.

### Typical Use Cases

- Local music player with touch screen
- SD card audio scanning and list playback
- On-screen player UI synchronized with playback state

### Runtime Flow

1. `app_start()` creates the event bus (`esp_event` task `mp_evt`), then initializes board → display → UI → playlist → playback engine → buttons in order
2. `esp_board_manager` initializes SD card, audio DAC, LCD, and touch
3. `esp_media_db` scans `/sdcard` for `.mp3`, `.aac`, and `.wav` files (scan depth is 1)
4. `esp_playlist` imports scan results and manages repeat modes (one / all / shuffle)
5. `esp_audio_simple_player` decodes asynchronously; PCM is written directly to the codec with `esp_codec_dev_write()`
6. `esp_lvgl_adapter` runs the LVGL task and shows a dark player page
7. The UI and the on-board key only post events (`music_player_event_t` in `app.h`); the playback engine handles them serially in the `mp_evt` task and pushes title / volume / play state back to the UI as state events

### File Structure

```
music_player/
├── assets/                 Place font.ttf here to embed it into the assets partition
├── main/
│   ├── app_main.c               Firmware entry, a single app_start() call
│   ├── app.c / app.h            Event bus definition + startup orchestration (read this first)
│   ├── music_player_config.h    Default volume, font path, log tags, and related macros
│   ├── music_player_board.c/h   Board peripheral init (SD card, codec)
│   ├── music_player_display.c/h LVGL adapter and display registration
│   ├── music_player_playlist.c/h Playlist: scanning, track order, repeat modes
│   ├── music_player_playback.c/h Playback engine: decoding, volume, progress
│   ├── music_player_ui.c/h       Player UI and touch controls
│   ├── music_player_buttons.c/h On-board PLAY key
│   ├── hello_wav_provision.c/h  Release the embedded hello.wav to the SD card
│   └── idf_component.yml
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32p4
├── sdkconfig.defaults.esp32s3
├── sdkconfig.defaults.esp32s31
├── pytest_music_player.py
├── README.md
└── README_CN.md
```

## Environment Setup

### Hardware Requirements

- microSD card (FAT filesystem)
- Speaker or headphones connected to the board audio output
- LCD and touch input devices

### Supported IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Copy `.mp3`, `.aac`, or `.wav` test files into the `/sdcard` mount point (UTF-8 filenames supported)
- To use FreeType for Chinese rendering, place `font.ttf` under `assets/` before build; it is embedded into the Flash assets partition (ensure redistribution is allowed). Without it, the built-in CJK font is used

## Build and Flash

### Build Preparation

Make sure ESP-IDF is configured before building this example. If it is already set up, enter the project directory directly. Otherwise, run the following scripts in the ESP-IDF root directory. See the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/) for the target chip you use.

```
./install.sh
. ./export.sh
```

Quick steps:

- Enter the example directory:

```
cd adf_examples/player/music_player
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager). The [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) helper tool is recommended as the default entry point.

- Install the helper in the activated ESP-IDF Python environment (once per environment):

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # run this command when an update is requested
```

- List the currently visible boards:

```bash
idf.py bmgr -l
```

Example output:

```text
ℹ️  Board Components:
  espressif/esp_boards:
    [1] esp32_c3_lyra
    [2] esp32_lyrat_4_3
    [3] esp32_lyrat_mini_1_1
    [4] esp32_p4_eye
    [5] esp32_p4_function_ev_board
    [6] esp32_s31_function_coreboard_1
    [7] esp32_s31_korvo_1
    [8] esp32_s3_box_3
    [9] esp32_s3_box_lite
    [10] esp32_s3_korvo_2_3
    [11] esp32_s3_lcd_ev_board
    [12] esp_vocat_1_0
    [13] esp_vocat_1_2
```

The example output above is based on the board list and ordering from `esp_boards` 0.5.2. Different `esp_boards` versions or custom board dependencies may change the list and indexes. Use the actual output of `idf.py bmgr -l` when selecting a board.

- Select a board:

```bash
idf.py bmgr -b <board_index|board_name>
```

For example, to select `esp32_p4_function_ev_board`:

```bash
idf.py bmgr -b 5
# or
idf.py bmgr -b esp32_p4_function_ev_board
```

On first invocation of `idf.py bmgr`, the component is downloaded automatically based on the `espressif/esp_board_manager` dependency declared in `main/idf_component.yml`.

> [!NOTE]
> To switch to a different board supported by `esp_board_manager`, repeat the same steps with the new board name or index.
> For a custom board, see [Creating a Board Guide](https://docs.espressif.com/projects/esp-board-manager/en/latest/create-board/index.html).
> For more information, see the [ESP Board Manager getting started guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

Default behavior is defined in `main/music_player_config.h` and `sdkconfig.defaults`; extra menuconfig is usually unnecessary. Common tunables:

- `MUSIC_PLAYER_DEFAULT_VOLUME`: default playback volume (currently 70)
- `MUSIC_PLAYER_FONT_PATH` / `MUSIC_PLAYER_FONT_SIZE`: FreeType font path and size (default `F:font.ttf`, 28)
- `MUSIC_PLAYER_SCAN_DEPTH`: media DB scan depth (currently 1, scanning `/sdcard`)
- `MUSIC_PLAYER_DEFAULT_TRACK_NAME`: track selected at boot (default `hello_1.wav`, falls back to the first track if not found)

For FreeType, built-in CJK font, and other LVGL options, use `idf.py menuconfig` under `Component config` → `ESP LVGL Adapter` and `LVGL configuration`.

### Build and Flash

- Build the example

```
idf.py build
```

- Flash and monitor (replace PORT with your serial port):

```
idf.py -p PORT flash monitor
```

- Press `Ctrl-]` to exit the monitor

## How to Use the Example

### Features and Usage

1. Copy music files into the `/sdcard` mount point or one of its first-level subdirectories (see `scan dir: /sdcard` in the log below)
2. After boot, the example scans the folder and stops in a ready state if tracks are found; playback starts only when you press PLAY (the UI shows no music available if nothing is found)
3. Use the bottom touch control bar:
   - Playlist / previous / play or pause / next / volume − / volume + / loop mode
   - Loop mode button cycles one-track repeat, list repeat, and shuffle
   - The board PLAY key (schematic ADC level about 1.65 V) works like the on-screen play button: press once to start, press again to pause
4. The top area shows the current song title (ellipsis mode, Chinese supported), repeat mode, and volume
5. The progress bar and time labels show playback progress
6. When the current track finishes, the next track starts automatically according to the selected mode

### Log Output

Third-party components (GMF, audio, LVGL, board manager) are clamped to WARN by default, and only a few `mp.*` lines
from this example remain enabled, so a normal boot prints a single line when startup completes:

```text
I (1892) mp.app: 启动完成，曲目 1 首，按 PLAY 键或屏幕播放键开始播放
```

Anything that goes wrong prints more, and the `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR` helpers automatically add
the function name and line number:

```text
E (1892) mp.player: music_player_playback.c:513 启动播放失败: 5，文件 /sdcard/test.mp3
```

To see more, change `ESP_LOG_WARN` to `ESP_LOG_INFO` in the first line of `app_log_level_init()` in `main/app.c`
to re-enable third-party logs. The `ESP_LOGD` details compiled into this example also require raising
`CONFIG_LOG_MAXIMUM_LEVEL` to DEBUG (`idf.py menuconfig` → Component config → Log output).

> Note: the ESP-IDF boot logs printed before `app_main()` are not affected by that setting.
## Troubleshooting

### Music Files Not Found

If scan fails or you see `未在 /sdcard 找到可播放音频`, verify that the microSD card is mounted and `/sdcard` contains `.mp3`, `.aac`, or `.wav` files.

### Playback Failure

If you see `跳过无法解码的文件` (skipping an undecodable file) or `启动播放失败` (playback start failed), check file integrity, supported format, and SD card connection.

### Chinese Text Rendering Issues

If the assets partition does not contain `font.ttf`, the example falls back to the built-in CJK font. For better Chinese rendering, place `font.ttf` in `assets/` and rebuild before flashing.

## References

- [ESP Audio Simple Player](https://components.espressif.com/components/espressif/esp_audio_simple_player)
- [ESP Board Manager](https://github.com/espressif/esp-board-manager)
- [ESP LVGL Adapter](https://components.espressif.com/components/espressif/esp_lvgl_adapter)

## Technical Support

- Forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- Issues: [GitHub issue](https://github.com/espressif/esp-adf/issues)

We will get back to you as soon as possible.
