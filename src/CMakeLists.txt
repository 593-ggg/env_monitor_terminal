# 自动收集src下所有.c和.cc源码 (ai模块是C++文件)
file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/network/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thread/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/font/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/image/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/device/*.c
)

# 生成可执行程序，程序名env_monitor_terminal，可自定义
add_executable(env_monitor_terminal ${SRC_FILES})

# 添加lvgl依赖
add_dependencies(env_monitor_terminal lvgl)

# 3rdparty和utils需要先编译出静态库
add_dependencies(env_monitor_terminal imageutils fileutils imagedrawing)

# 链接库
target_link_libraries(env_monitor_terminal
    lvgl
    lvgl::examples
    lvgl::demos
    lvgl::thorvg
    # utils里编译出来的静态库
    imageutils
    fileutils
    imagedrawing
    # 3rdparty里链接出来的运行时库
    ${LIBRKNNRT}
    ${LIBRGA}
    # 系统库
    m
    pthread
    dl
)

# 头文件搜索路径 (3rdparty的RKNN和RGA头文件)
target_include_directories(env_monitor_terminal PRIVATE
    ${LIBRKNNRT_INCLUDES}
    ${LIBRGA_INCLUDES}
    ${STB_INCLUDES}
    ${LIBJPEG_INCLUDES}
)

# 添加自定义目标以便运行
add_custom_target(run 
    COMMAND ${EXECUTABLE_OUTPUT_PATH}/env_monitor_terminal 
    DEPENDS env_monitor_terminal
)