#ifndef __GY39_H__
#define __GY39_H__

// 依赖串口模块
#include "serial.h"

// ===================== 帧协议宏定义 =====================
// GY39串口数据帧固定帧头，每一条有效数据开头都是两个0x5A
#define GY39_HEAD0 0x5A
#define GY39_HEAD1 0x5A

// 帧第2字节：代表当前帧是什么数据
#define GY39_TYPE_LUX      0x15  // 帧类型：光照强度数据
#define GY39_TYPE_ENV      0x45  // 帧类型：温度、气压、湿度、海拔数据
#define GY39_TYPE_IICADDR  0x55  // 帧类型：模块IIC地址（暂时不用）

// 发给GY39的控制指令统一帧头
#define GY39_CMD_HEAD 0xA5

// 自动输出配置寄存器每一位含义
#define GY39_AUTO_EN    (1 << 7) // bit7=1：上电自动持续上报数据
#define GY39_BME_EN     (1 << 1) // bit1=1：开启温湿度气压海拔输出
#define GY39_MAX_EN     (1 << 0) // bit0=1：开启光照强度输出

// ===================== 数据存储结构体 =====================
// 光照强度专用结构体
typedef struct Gy39LuxData
{
    float lux;  // 光照值，单位：lux（勒克斯）
} Gy39LuxData;

// 环境综合数据结构体：温湿度、气压、海拔
typedef struct Gy39EnvData
{
    float temp;     // 温度，单位：℃
    float press;    // 气压，单位：hPa（百帕）
    float hum;      // 湿度，单位：%RH（百分比）
    float alt;      // 海拔，单位：m（米）
} Gy39EnvData;

/**
 * @brief 发送指令配置GY39自动上报哪些数据
 * @param fd 串口文件描述符（init_serial打开得到）
 * @param cfg 组合配置：GY39_AUTO_EN | GY39_BME_EN | GY39_MAX_EN
 * @return 发送成功返回0，失败返回-1
 */
int gy39_set_auto_output(int fd, unsigned char cfg);

/**
 * @brief 主动查询一次光照强度数据
 * @param fd 串口fd
 * @param out 输出结构体，存放读到的光照数值
 * @return 成功0，失败-1
 */
int gy39_query_lux(int fd, Gy39LuxData *out);

/**
 * @brief 主动查询一次温湿度、气压、海拔数据
 * @param fd 串口fd
 * @param out 输出结构体，存放读到的环境数值
 * @return 成功0，失败-1
 */
int gy39_query_env(int fd, Gy39EnvData *out);

/**
 * @brief 解析串口收到的原始GY39一整帧数据
 * @param buf 串口接收的原始字节缓冲区
 * @param len 当前缓冲区有效字节长度
 * @param lux 光照结构体指针，不需要可传NULL
 * @param env 环境结构体指针，不需要可传NULL
 * @return 返回值含义：
 *          0 = 未读到完整有效帧
 *          1 = 解析成功，本次是光照数据
 *          2 = 解析成功，本次是环境数据
 *         -1 = 校验和错误，数据丢包/错乱
 */
int gy39_parse_frame(unsigned char *buf, int len, Gy39LuxData *lux, Gy39EnvData *env);

#endif