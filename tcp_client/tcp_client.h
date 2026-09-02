#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include "tcp_protocol.h"
#include "tcp_cmd_parser.h"
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

// 服务端连接配置
extern char *server_ip;
extern int server_port;



/**
 * @brief 初始化客户端，连接服务端，创建recv/send双线程
 * @return 成功返回ClientConn指针，失败NULL
 */
ClientConn* client_create(void);

/**
 * @brief 销毁客户端上下文，关闭socket，回收线程与内存
 * @param conn 客户端句柄
 */
void client_destroy(ClientConn* conn);

/**
 * @brief 解析终端命令并送入发送队列
 * @param conn 客户端句柄
 * @param cmd 终端输入字符串(/msg /file /heartbeat)
 * @return 0成功，负数代表解析失败
 */
int client_input_cmd(ClientConn* conn, const char* cmd);

/**
 * @brief 接收线程：专职读socket，按pkt_type路由（RESPONSE→唤醒send，推送→处理）
 */
void* client_recv_thread(void* arg);

/**
 * @brief 发送线程：从队列取包发送，用条件变量等待应答
 */
void* client_send_thread(void* arg);

#endif
