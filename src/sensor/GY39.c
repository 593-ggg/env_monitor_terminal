#include "gy39.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief 内部私有函数：计算一组数据的累加校验和（只保留低8位）
 * @param data 待计算字节数组
 * @param len 数组长度
 * @return 1字节校验和sum
 */
static unsigned char gy39_calc_sum(unsigned char *data, int len)
{
    unsigned char sum = 0;
    // 把所有字节全部相加，自动溢出截断到8位
    for (int i = 0; i < len; i++)
        sum += data[i];
    return sum;
}

int gy39_set_auto_output(int fd, unsigned char cfg)
{
    // 指令格式：帧头0xA5 + 配置字节 + 校验和
    unsigned char cmd[3];
    cmd[0] = GY39_CMD_HEAD; // 指令固定开头0xA5
    cmd[1] = cfg;           // 我们要设置的输出开关
    cmd[2] = gy39_calc_sum(cmd, 2); // 前两个字节相加得到校验和

    // 调用串口发送函数把3字节指令发给GY39
    // serial_send 返回发送字节数，约定 0 成功 / -1 失败
    int sent = serial_send(fd, (char *)cmd, 3);
    return (sent == 3) ? 0 : -1;
}

int gy39_query_lux(int fd, Gy39LuxData *out)
{
    // 空指针判断，防止程序崩溃
    if (!out)
        return -1;

    // 查询光照固定指令：0xA5 0x51 0xF6
    unsigned char cmd[3] = {0xA5, 0x51, 0xF6};
    serial_send(fd, (char *)cmd, 3);

    // 清空上次旧数据
    memset(out, 0, sizeof(Gy39LuxData));
    return 0;
}

int gy39_query_env(int fd, Gy39EnvData *out)
{
    if (!out)
        return -1;

    // 查询环境数据固定指令：0xA5 0x52 0xF7
    unsigned char cmd[3] = {0xA5, 0x52, 0xF7};
    serial_send(fd, (char *)cmd, 3);

    memset(out, 0, sizeof(Gy39EnvData));
    return 0;
}

int gy39_parse_frame(unsigned char *buf, int len, Gy39LuxData *lux, Gy39EnvData *env)
{
    // 校验帧头：GY39每帧固定前两个字节为0x5A 0x5A，不符则不是有效帧
    if (buf[0] != GY39_HEAD0 || buf[1] != GY39_HEAD1)
        return 0;

    // 至少需要前4字节：帧头2字节 + type1字节 + data_len1字节，不足无法计算完整帧长度
    if (len < 4)
        return 0;

    // 取出第3字节：数据类型标识
    unsigned char type = buf[2];
    // 取出第4字节：本帧携带的数据字节数量data_len
    unsigned char data_len = buf[3];

    // 根据手册公式计算完整一帧总字节长度
    // 前4字节(0x5A 0x5A type len) + data_len个数据字节 + 1字节校验和 = data_len + 5
    int full_frame_len = data_len + 5;

    // 当前缓冲区字节不足完整一帧，数据未收齐，不解析直接返回
    if (len < full_frame_len)
        return 0;

    // 计算校验和：将帧内除最后1字节校验和外所有字节累加，保留低8位
    unsigned char calc_sum = 0;
    for (int i = 0; i < full_frame_len - 1; i++)
        calc_sum += buf[i];
    // 取出模块自带校验和(最后一字节)
    unsigned char recv_sum = buf[full_frame_len - 1];

    // 本地计算校验和与模块发送校验和不一致，代表数据丢包/干扰出错
    if (calc_sum != recv_sum)
    {
        printf("GY39校验错误\n");
        return -1;
    }

    // 分支1：解析光照强度数据 type=0x15，data_len固定4字节 
    if (type == GY39_TYPE_LUX && data_len == 4 && lux != NULL)
    {
        // 4个数据字节拼接为32位无符号整数
        unsigned int raw = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
        // 手册规定原始数值除以100得到真实光照lux
        lux->lux = raw / 100.0f;
        return 1; // 返回1标识本次解析出光照数据
    }

    // 分支2：解析温湿度、气压、湿度、海拔 type=0x45，data_len固定10字节 
    if (type == GY39_TYPE_ENV && data_len == 10 && env != NULL)
    {
        // 温度：buf4高8位 + buf5低8位 拼接16位整数，除以100得到摄氏度
        unsigned short t = (buf[4] << 8) | buf[5];
        env->temp = t / 100.0f;

        // 气压：4字节拼接32位整数，除以100得到百帕hPa
        unsigned int p = (buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | buf[9];
        env->press = p / 100.0f;

        // 湿度：buf10高8位 + buf11低8位 拼接16位整数，除以100得到百分比湿度
        unsigned short hum = (buf[10] << 8) | buf[11];
        env->hum = hum / 100.0f;

        // 海拔：buf12高8位 + buf13低8位 拼接16位整数，单位米无需换算
        unsigned short alt = (buf[12] << 8) | buf[13];
        env->alt = alt;
        return 2; // 返回2标识本次解析出环境数据
    }

    // 其他帧类型(IIC地址0x55等)暂时不处理，返回0
    return 0;
}