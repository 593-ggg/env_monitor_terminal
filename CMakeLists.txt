cmake_minimum_required(VERSION 3.10)

# 设置交叉编译
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
# /usr/local/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# 项目名称
project(env_monitor_terminal)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 新增: YOLOv8 需要的芯片宏 ====================
# 3rdparty/CMakeLists.txt 里的库路径判断依赖这个变量
set(TARGET_SOC "rk356x")
# utils/CMakeLists.txt 里的 librga 选择需要架构判断
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_LIB_ARCH aarch64)
else()
    set(TARGET_LIB_ARCH armhf)
endif()

# 设置可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/ai
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/camera
    ${PROJECT_SOURCE_DIR}/src/network
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/sensor
    ${PROJECT_SOURCE_DIR}/src/thread
    ${PROJECT_SOURCE_DIR}/src/font
    ${PROJECT_SOURCE_DIR}/src/tests
    ${PROJECT_SOURCE_DIR}/src/image
    ${PROJECT_SOURCE_DIR}/src/device
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/lvgl
    ${PROJECT_SOURCE_DIR}
)

# 添加子目录
add_subdirectory(3rdparty)
add_subdirectory(utils)
add_subdirectory(lvgl)
add_subdirectory(src)
add_subdirectory(tcp_client)