# TCP客户端编译配置
# 生成可执行文件 tcp_client，输出到项目根目录的bin目录

# 客户端源文件
set(CLIENT_SRCS
    client_main.c
    tcp_cmd_parser.c
    tcp_client.c
    tcp_protocol.c
)

# 可执行文件名称
set(CLIENT_TARGET tcp_client)

# 创建可执行文件目标
add_executable(${CLIENT_TARGET} ${CLIENT_SRCS})

# 头文件搜索路径
target_include_directories(${CLIENT_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/network
    ${PROJECT_SOURCE_DIR}/src
)

# 链接 pthread 库（用于多线程）
target_link_libraries(${CLIENT_TARGET} pthread)

# 设置输出目录到项目根目录的bin目录
set_target_properties(${CLIENT_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

# 设置编译选项
target_compile_options(${CLIENT_TARGET} PRIVATE
    -Wall
    -Wextra
    -O2
)
