#ifndef __SERIAL_H__
#define __SERIAL_H__

// 串口设备节点
#define UART0  "/dev/ttyS0"
#define UART1  "/dev/ttyS1"
#define UART3  "/dev/ttyS3"
#define UART4  "/dev/ttyS4"

/**
 * @brief 打开并初始化串口
 * @param file 串口设备路径
 * @param baudrate 波特率：9600/19200/38400/115200
 * @return 成功返回串口fd，失败返回-1
 */
int init_serial(const char *file, int baudrate);

/**
 * @brief 串口发送数据
 * @param fd 串口文件描述符
 * @param buf 待发送数据缓冲区
 * @param len 数据长度
 * @return 成功返回发送字节数，失败返回-1
 */
int serial_send(int fd, const char *buf, int len);

/**
 * @brief 串口阻塞读取数据
 * @param fd 串口文件描述符
 * @param buf 接收缓冲区
 * @param len 最大读取长度
 * @return 成功返回读到字节数，失败返回-1
 */
int serial_recv(int fd, char *buf, int len);

/**
 * @brief 关闭串口设备
 * @param fd 串口文件描述符
 * @return 无
 */
void serial_close(int fd);

#endif