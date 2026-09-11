# 工程顶层构建入口：只负责声明工程名并把构建流程交给 ESP-IDF 的 project.cmake，
# 具体的组件、源文件、依赖都写在 main/CMakeLists.txt 与 main/idf_component.yml 里。
cmake_minimum_required(VERSION 3.16)

# 开启 ccache 编译缓存：ccache 以「源文件内容 + 编译参数」为 key 缓存编译产物，
# 未改动的文件再次编译时直接命中缓存，可显著缩短重复的全量构建和改配置后的重建。
# 注意：本机 ccache 在 D:/Espressif/tools/ccache/4.12.1/... ，需要该目录在 PATH 中
#       （用 ESP-IDF 的 export.ps1 打开终端即会带上）；若找不到 ccache，
#       ESP-IDF 只会打印一条 warning，不会影响构建。
# 注意：idf.py 每次调用都会用 -DCCACHE_ENABLE=<--ccache/--no-ccache 的取值> 覆盖此项，
#       所以这里用 FORCE 写入缓存变量，保证直接执行 `idf.py build` 也能吃到 ccache；
#       如需关闭 ccache，把下面的 1 改成 0 即可。
set(CCACHE_ENABLE 1 CACHE BOOL "使用 ccache 加速重复编译" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(music_player)
