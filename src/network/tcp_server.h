#ifndef TCP_SERVER_H
#define TCP_SERVER_H
#include "tcp_protocol.h"
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/socket.h>
// 服务端全局配置（ClientConn结构体已在tcp_protocol.h定义）
#define SERVER_LISTEN_PORT 8888  // 监听端口
#define SERVER_BACKLOG 10        // 监听队列长度
#define CLIENT_RECV_BUF_LEN 4096 // 客户端接收缓冲区大小
#define MAX_CLIENTS 64           // 最大连接数

// 全局服务器关闭状态标记（新增，用于防重入、屏蔽关机重复广播）
extern bool g_server_shutting_down;

/**
 * @brief 启动TCP服务端，循环accept接收客户端连接
 */
void server_start(void);
/**
 * @brief 释放单个客户端连接所有资源、关闭fd、回收线程
 * @param conn 客户端上下文指针
 */
void client_conn_destroy(ClientConn* conn);

/************************ 客户端管理器API（线程安全） ************************/
/**
 * @brief 初始化客户端管理器，创建互斥锁和链表头
 *        必须在server_start之前调用一次
 */
void client_mgr_init(void);
/**
 * @brief 将连接加入管理器链表
 * @param conn 新建连接
 * @return 0成功，-1达到上限
 */
int client_mgr_add(ClientConn* conn);
/**
 * @brief 将连接从管理器链表移除（断开时调用）
 * @param conn 待移除连接
 */
void client_mgr_remove(ClientConn* conn);
/**
 * @brief 设置客户端标识（id:xxx命令处理）
 * @param conn 目标连接
 * @param id 新标识字符串
 * @return 0成功，-1 ID已被占用
 */
int client_mgr_set_id(ClientConn* conn, const char* id);
/**
 * @brief 按标识查找客户端连接（不持有锁，仅内部使用或短查询）
 * @param id 客户端标识
 * @return 连接指针，未找到返回NULL
 */
ClientConn* client_mgr_find_by_id(const char* id);
/**
 * @brief 向所有在线客户端广播数据包（排除exclude_fd，-1不排除）
 * @param pkt 待广播数据包
 * @param exclude_fd 排除的fd，传-1不排除任何客户端
 * @return 成功发送的客户端数量
 */
int client_mgr_broadcast(Packet* pkt, int exclude_fd);
/**
 * @brief 向指定标识的客户端发送数据包
 * @param id 目标客户端标识
 * @param pkt 待发送数据包
 * @return 0成功，-1目标不在线
 */
int client_mgr_send_to_id(const char* id, Packet* pkt);
/**
 * @brief 获取当前在线客户端数量
 * @return 客户端数量
 */
int client_mgr_count(void);
/**
 * @brief 打印所有在线客户端列表（fd + id）
 */
void client_mgr_list(void);
/**
 * @brief 将在线客户端列表字符串发送到指定连接
 * @param conn 目标连接
 */
void client_mgr_list_to(struct ClientConn* conn);
/**
 * @brief 断开指定客户端的连接（按ID查找，阻塞等待线程退出）
 * @param id 客户端标识（如 "fd4" 或自定义ID）
 */
void client_mgr_disconnect(const char* id);
/**
 * @brief 关闭服务器，断开所有客户端连接，释放所有资源
 *        调用后不能再用任何 client_mgr_* 函数
 */
void server_shutdown(void);

/************************ UI查询接口（供ui_server_src调用） ************************/

// 单条客户端信息（UI层使用，不暴露ClientConn内部）
typedef struct {
    char id[64];            // 客户端标识
    char ip[32];            // 客户端IP地址
    uint16_t port;          // 客户端端口号
} ClientInfo;

/**
 * @brief 获取所有在线客户端信息列表（线程安全拷贝）
 * @param out_buf 输出缓冲区数组
 * @param max_count 缓冲区最大容量
 * @return 实际写入的客户端数量（<=max_count）
 */
int client_mgr_get_info_list(ClientInfo* out_buf, int max_count);
/************************ 文件传输会话管理API ************************/
/**
 * @brief 设置文件传输会话（客户端之间传输）
 * @param sender_fd 发送者文件描述符
 * @param target 接收者连接
 */
void server_file_session_set(int sender_fd, ClientConn* target);
/**
 * @brief 设置文件上传会话（客户端上传到服务端）
 * @param uploader_fd 上传者文件描述符
 * @param file_fd 服务端文件描述符
 * @param filename 上传文件名
 */
void server_upload_session_set(int uploader_fd, int file_fd, const char* filename);
#endif // TCP_SERVER_H