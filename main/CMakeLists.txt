# 把 hello.wav 嵌入应用镜像：构建系统会据此生成 _binary_hello_wav_start / _binary_hello_wav_end
# 两个符号，应用启动后由 hello_wav_provision() 把它释放到 SD 卡，免去用读卡器拷文件。
idf_component_register(SRC_DIRS "./" INCLUDE_DIRS "./" EMBED_FILES "hello.wav")

# 把 assets/ 目录下的资源打包进独立的 assets 分区（见 partitions.csv），
# 运行时由 esp_mmap_assets 以内存映射方式访问，并挂载成 LVGL 的 F: 盘。
set(assets_dir "${CMAKE_CURRENT_LIST_DIR}/../assets")
if(EXISTS "${assets_dir}")
    # 目前只交给 mmap 处理 .ttf，即 assets/font.ttf，供 FreeType 渲染中文
    spiffs_create_partition_assets(assets "${assets_dir}" FLASH_IN_PROJECT
                                   MMAP_FILE_SUPPORT_FORMAT ".ttf")
    add_dependencies(${COMPONENT_LIB} assets_assets_bin)
endif()
