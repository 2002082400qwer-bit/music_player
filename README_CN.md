# SD 卡音乐播放器

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

本例程演示一个带触摸屏 UI 的 SD 卡音乐播放器。例程可扫描 microSD 卡中的本地音乐文件，在屏幕上显示播放器界面、歌曲信息和播放控制入口，并通过音频输出设备播放音乐。

### 典型场景

- 带触摸屏的本地音乐播放器
- SD 卡音频文件扫描与列表播放
- 屏幕播放器界面与音频播放状态联动

### 运行机制

1. `app_start()` 创建事件总线（`esp_event` 的事件任务 `mp_evt`），再按顺序初始化板级设备 → 显示 → 界面 → 曲库 → 播放引擎 → 实体按键
2. `esp_board_manager` 初始化 SD 卡、音频 DAC、LCD 与触摸
3. `esp_media_db` 扫描 SD 卡挂载点 `/sdcard` 下的 `.mp3`、`.aac`、`.wav` 文件（扫描深度为 1）
4. `esp_playlist` 导入扫描结果并管理播放模式（单曲循环 / 列表循环 / 随机播放）
5. `esp_audio_simple_player` 异步解码，PCM 通过 `esp_codec_dev_write()` 直接写入 codec 输出
6. `esp_lvgl_adapter` 运行 LVGL 任务，显示深色播放页
7. 界面与实体按键只往事件总线投递事件（`app.h` 里的 `music_player_event_t`），播放引擎在 `mp_evt` 任务里串行处理，再把歌名 / 音量 / 播放状态以状态事件推回界面

### 文件结构

```
music_player/
├── assets/                 font.ttf 可放在此目录，编译时打入 assets 分区
├── main/
│   ├── app_main.c               固件入口，只有一行 app_start()
│   ├── app.c / app.h            事件总线定义 + 初始化编排（先看这里）
│   ├── music_player_config.h    默认音量、字体路径、日志标签等宏
│   ├── music_player_board.c/h   板级设备初始化（SD 卡、codec）
│   ├── music_player_display.c/h LVGL adapter 与显示注册
│   ├── music_player_playlist.c/h 曲库：扫描、曲目顺序、循环模式
│   ├── music_player_playback.c/h 播放引擎：解码、音量、进度
│   ├── music_player_ui.c/h      播放页 UI 与触摸控制
│   ├── music_player_buttons.c/h 板载 PLAY 实体键
│   ├── hello_wav_provision.c/h  把内嵌 hello.wav 释放到 SD 卡
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

## 环境配置

### 硬件要求

- microSD 卡（FAT 文件系统）
- 扬声器或耳机（连接到开发板音频输出）
- LCD 屏与触摸输入设备

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- 在 microSD 卡挂载点 `/sdcard` 下放入 `.mp3`、`.aac` 或 `.wav` 测试文件（支持中文文件名）
- 如需使用 FreeType 显示中文，将 `font.ttf` 放入 `assets/` 目录；编译时会打入 Flash 的 assets 分区（须确认字体授权可再分发）。未提供时使用内置 CJK 字体

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请根据目标芯片参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```
cd adf_examples/player/music_player
```

本示例使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 管理板级资源。推荐安装辅助工具 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) 作为默认入口。

- 在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # 当提示需要更新时执行此命令
```

- 列出当前可见的开发板：

```bash
idf.py bmgr -l
```

输出示例：

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

以上输出示例基于 `esp_boards` 0.5.2 的开发板列表和排序。不同 `esp_boards` 版本或自定义开发板依赖可能会使列表和序号变化，使用时以 `idf.py bmgr -l` 的实际输出为准。

- 选择开发板：

```bash
idf.py bmgr -b <board_index|board_name>
```

例如选择 `esp32_p4_function_ev_board`：

```bash
idf.py bmgr -b 5
# 或
idf.py bmgr -b esp32_p4_function_ev_board
```

首次执行 `idf.py bmgr` 时，组件会根据本工程 `main/idf_component.yml` 中声明的 `espressif/esp_board_manager` 依赖自动下载。

> [!NOTE]
> 如果切换为其他 `esp_board_manager` 支持的开发板，请按相同步骤执行并替换板型名称/索引。
> 自定义开发板请参考 [创建开发板指南](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/create-board/index.html)。
> `esp_board_manager` 更多信息请参考 [ESP_BOARD_MANAGER 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)

### 项目配置

本例程默认行为由 `main/music_player_config.h` 与 `sdkconfig.defaults` 决定，通常无需额外 menuconfig。常用可调项如下：

- `MUSIC_PLAYER_DEFAULT_VOLUME`：默认播放音量（当前为 70）
- `MUSIC_PLAYER_FONT_PATH` / `MUSIC_PLAYER_FONT_SIZE`：FreeType 字体路径与字号（默认 `F:font.ttf`、28）
- `MUSIC_PLAYER_SCAN_DEPTH`：媒体库扫描深度（当前为 1，对应挂载点 `/sdcard`）
- `MUSIC_PLAYER_DEFAULT_TRACK_NAME`：开机默认选中的曲目文件名（默认 `hello_1.wav`，列表里找不到时用第一首）

如需启用或关闭 FreeType、CJK 内置字体等 LVGL 相关选项，可在 `idf.py menuconfig` 中查看 `Component config` → `ESP LVGL Adapter` 与 `LVGL configuration`。

### 编译与烧录

- 编译示例程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl-]`

## 如何使用例程

### 功能和用法

1. 将音乐文件放入 microSD 卡挂载点 `/sdcard` 或其一层子目录（见下文 log 中的 `scan dir: /sdcard`）
2. 上电后例程自动扫描；找到音乐后停在就绪状态并显示当前曲目，不会自动播放（没找到则 UI 显示“未找到音乐”）
3. 触摸屏底部控制栏：
   - 播放列表 / 上一首 / 播放或暂停 / 下一首 / 音量 − / 音量 + / 循环模式
   - 循环模式按钮：依次切换单曲循环、列表循环、随机播放
   - 板载实体 PLAY 键（原理图 ADC 分压约 1.65V）与屏幕播放键等效：按一次开始播放，再按一次暂停
4. 顶部显示当前歌名（固定宽度省略显示，支持中文）、播放模式与音量
5. 进度条与时间显示播放进度
6. 当前曲目播放结束后自动播放下一首（按当前模式）

### 日志输出

本工程默认把第三方组件（GMF、音频、LVGL、板级管理器等）的日志压到 WARN，只保留自己 `mp.*` 标签下的少量关键信息，
因此正常开机只会在初始化结束时打印一行：

```text
I (1892) mp.app: 启动完成，曲目 1 首，按 PLAY 键或屏幕播放键开始播放
```

歌曲放不出来、扫描失败这类问题才会有额外输出，且 `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`
这类检查宏会自动带上函数名与行号，方便定位：

```text
E (1892) mp.player: music_player_playback.c:513 启动播放失败: 5，文件 /sdcard/test.mp3
```

需要看更多日志时，把 `main/app.c` 中 `app_log_level_init()` 第一行的 `ESP_LOG_WARN` 改成 `ESP_LOG_INFO`
即可放开第三方组件日志；本工程源码里的 `ESP_LOGD` 细节日志还需要把
`CONFIG_LOG_MAXIMUM_LEVEL` 提到 DEBUG（`idf.py menuconfig` → Component config → Log output）才会被编译进去。

> 说明：ESP-IDF 自身在 `app_main()` 之前的引导日志不受上面的设置影响，仍然会正常打印。
## 故障排除

### 未找到音乐文件

如果日志提示 `未在 /sdcard 找到可播放音频`，请确认 microSD 卡已正确挂载，且 `/sdcard` 下存在 `.mp3`、`.aac` 或 `.wav` 文件。

### 播放失败

如果日志出现 `跳过无法解码的文件` 或 `启动播放失败`，请检查文件是否损坏、格式是否受支持，以及 SD 卡接触是否良好。

### 中文显示异常

若 assets 分区未包含 `font.ttf`，例程会回退到内置 CJK 字体。如需更好的中文显示，请在编译前将 `font.ttf` 放入 `assets/` 目录后重新编译并烧录。

## 参考资料

- [ESP Audio Simple Player](https://components.espressif.com/components/espressif/esp_audio_simple_player)
- [ESP Board Manager](https://github.com/espressif/esp-board-manager)
- [ESP LVGL Adapter](https://components.espressif.com/components/espressif/esp_lvgl_adapter)

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-adf/issues)

我们会尽快回复。
