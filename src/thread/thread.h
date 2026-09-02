#ifndef __THREAD_H__
#define __THREAD_H__

#include <pthread.h>
#include <stdbool.h>
#include "GY39.h"
#include "yolov8_wrap.h"

/* ==================== 摄像头线程接口 ==================== */

/**
 * @brief 启动摄像头采集线程
 * @return 0 成功, -1 失败
 */
int cam_thread_start(void);

/**
 * @brief 停止摄像头采集线程 (阻塞等待线程退出)
 */
void cam_thread_stop(void);

/**
 * @brief 查询摄像头线程是否正在运行
 */
bool cam_thread_is_running(void);

/* ==================== GY39 传感器线程接口 ==================== */

/**
 * @brief 启动 GY39 读取线程 (打开串口 + 配置自动上报)
 * @return 0 成功, -1 失败
 */
int gy39_thread_start(void);

/**
 * @brief 停止 GY39 读取线程 (阻塞等待线程退出, 关闭串口)
 */
void gy39_thread_stop(void);

/**
 * @brief 查询 GY39 线程是否正在运行
 */
bool gy39_thread_is_running(void);

/**
 * @brief 获取最新光照数据 (线程安全)
 * @param out 输出结构体指针
 * @return 0 成功, -1 失败
 */
int gy39_get_lux_data(Gy39LuxData *out);

/**
 * @brief 获取最新环境数据 (线程安全)
 * @param out 输出结构体指针
 * @return 0 成功, -1 失败
 */
int gy39_get_env_data(Gy39EnvData *out);

/* ==================== AI (YOLOv8) 推理线程接口 ==================== */

/**
 * @brief 启动 AI 推理线程 (加载 YOLOv8 模型 + 周期性推理)
 * @return 0 成功, -1 失败
 */
int ai_thread_start(void);

/**
 * @brief 停止 AI 推理线程 (阻塞等待线程退出, 释放模型资源)
 */
void ai_thread_stop(void);

/**
 * @brief 查询 AI 线程是否正在运行
 */
bool ai_thread_is_running(void);

/**
 * @brief 获取最新目标检测结果 (线程安全, 带互斥保护)
 * @param out 输出检测结果 (count + items 数组)
 * @return 0 成功, -1 失败
 */
int ai_thread_get_result(yolov8_result_t *out);

/* ==================== TCP 服务端线程接口 ==================== */

// ui层指令枚举
enum TcpUICmd
{
    TCP_UI_CMD_START     = 0,    // 启动服务器
    TCP_UI_CMD_CLOSE     = 1,    // 关闭服务器
    TCP_UI_CMD_DISCONNECT = 2,   // 断开指定客户端连接
};

/**
 * @brief 查询 TCP 服务端线程是否正在运行
 */
bool tcp_server_is_running(void);

/**
 * @brief 解析ui层指令（无参数命令：启动/关闭服务器）
 * @param cmd 指令枚举
 * @return 0 成功, -1 失败
 */
int tcp_server_parse_ui_cmd(enum TcpUICmd cmd);

/**
 * @brief 断开指定客户端连接（UI层调用，通过ID查找）
 * @param client_id 客户端标识字符串（如 "fd4" 或自定义ID）
 * @return 0 成功, -1 服务器未运行或客户端不存在
 */
int tcp_server_disconnect_client(const char* client_id);

#endif
