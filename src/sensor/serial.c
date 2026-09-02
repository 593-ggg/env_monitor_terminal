#include "serial.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

/**
 * @brief 打开并初始化串口
 * @param file 串口设备路径
 * @param baudrate 波特率：9600/19200/38400/115200
 * @return 成功返回串口fd，失败返回-1
 */
int init_serial(const char *file, int baudrate)
{
    int fd = open(file, O_RDWR);
    if (fd == -1)
    {
        perror("[SERIAL] open serial failed");
        return -1;
    }

    struct termios ser_cfg;
    memset(&ser_cfg, 0, sizeof(struct termios));

    // 本地模式、允许接收
    ser_cfg.c_cflag |= CLOCAL | CREAD;
    // 清空数据位、关闭硬件流控
    ser_cfg.c_cflag &= ~CSIZE;
    ser_cfg.c_cflag &= ~CRTSCTS;
    ser_cfg.c_cflag |= CS8;    // 8数据位
    ser_cfg.c_cflag &= ~CSTOPB;// 1停止位
    ser_cfg.c_cflag &= ~PARENB;// 无校验
    

    // 设置波特率
    switch (baudrate)
    {
        case 9600:
            cfsetospeed(&ser_cfg, B9600);
            cfsetispeed(&ser_cfg, B9600);
            break;
        case 19200:
            cfsetospeed(&ser_cfg, B19200);
            cfsetispeed(&ser_cfg, B19200);
            break;
        case 38400:
            cfsetospeed(&ser_cfg, B38400);
            cfsetispeed(&ser_cfg, B38400);
            break;
        case 115200:
            cfsetospeed(&ser_cfg, B115200);
            cfsetispeed(&ser_cfg, B115200);
            break;
        default:
            printf("[SERIAL] unsupported baudrate\n");
            close(fd);
            return -1;
    }

    // 清空串口缓冲区
    tcflush(fd, TCIFLUSH);
    // 立即生效配置
    tcsetattr(fd, TCSANOW, &ser_cfg);

    printf("[SERIAL] serial open success: %s, baud %d\n", file, baudrate);
    return fd;
}

/**
 * @brief 串口发送数据
 * @param fd 串口文件描述符
 * @param buf 待发送数据缓冲区
 * @param len 数据长度
 * @return 成功返回发送字节数，失败返回-1
 */
int serial_send(int fd, const char *buf, int len)
{
    if (fd < 0 || buf == NULL || len <= 0)
        return -1;
    return write(fd, buf, len);
}

/**
 * @brief 串口阻塞读取数据
 * @param fd 串口文件描述符
 * @param buf 接收缓冲区
 * @param len 最大读取长度
 * @return 成功返回读到字节数，失败返回-1
 */
int serial_recv(int fd, char *buf, int len)
{
    if (fd < 0 || buf == NULL || len <= 0)
        return -1;
    return read(fd, buf, len);
}

/**
 * @brief 关闭串口设备
 * @param fd 串口文件描述符
 * @return 无
 */
void serial_close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
        printf("[SERIAL] serial closed\n");
    }
}