#ifndef TCP_PROTOCOL_H
#define TCP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

/************************ 协议全局宏定义 ************************/
// 数据包魔数，服务端校验标识
#define PACKET_MAGIC     0xAA55CCDDU
// 包头固定字节长度 32字节
#define HEADER_SIZE      32U
// 单包最大包体 512KB，防止超大内存占用
#define MAX_BODY_SIZE    (512 * 1024U)
// 文件上传分片固定大小 256KB
#define FILE_CHUNK_SIZE  (256 * 1024U)

// 客户端标识最大长度
#define CLIENT_ID_LEN    64
// 发送队列容量
#define SEND_QUEUE_CAP   32

/************************ 数据包类型枚举（包头携带，二进制传输标识） ************************/
enum PKT_TYPE {
    PKT_INVALID         = 0,    // 非法数据包
    PKT_MSG_TEXT        = 1,    // 文本承载包（所有聊天/管理指令都放该包body）
    PKT_FILE_META       = 2,    // 文件上传元信息包
    PKT_FILE_DATA       = 3,    // 文件二进制分片包
    PKT_FILE_END        = 4,    // 文件上传结束标记包
    PKT_HEARTBEAT       = 5,    // 心跳保活包
    PKT_ERR             = 6     // 错误应答包（body 携带错误描述，pkt_sub_type 携带错误类型）
    };

/************************ 错误包子类型枚举（pkt_sub_type 字段） ************************/
enum ERR_TYPE {
    ERR_GENERAL          = 0,  // 通用错误
    ERR_FILE_NOT_FOUND   = 1,  // 文件不存在
    ERR_FILE_OPEN_FAILED = 2,  // 文件打开失败
    ERR_FILE_WRITE_FAILED = 3, // 文件写入失败
    ERR_BUSY             = 4,  // 服务端繁忙
    ERR_PERMISSION       = 5,  // 权限不足
};

/************************ 包头校验错误码 ************************/
#define RESP_OK             0
#define RESP_ERR_MAGIC      -1
#define RESP_ERR_CMD        -2
#define RESP_ERR_BODY_LIMIT -3

/************************ 包头结构体 固定32字节单字节对齐 ************************/
#pragma pack(1)
typedef struct {
    uint32_t magic;        // 魔数 网络字节序
    uint16_t pkt_type;     // 数据包类型 网络字节序
    uint32_t body_len;     // 包体长度 网络字节序
    uint8_t  pkt_sub_type; // 当前类型数据包的细类类型  
    uint64_t reserved;     // 预留8字节扩展位
    uint8_t  reserve[13];  // 填充字节，保证结构体总大小严格32字节
} PacketHeader;
#pragma pack()



/************************ 统一数据包封装结构体（包头+动态包体） ************************/
typedef struct {
    PacketHeader hdr;       // 固定32字节包头
    uint8_t*     body;      // 动态分配包体缓冲区，body_len=0时为NULL
    uint32_t     body_len;  // 主机序包体有效长度，缓存减少字节序转换
} Packet;

/************************ 连接状态枚举（业务层状态机） ************************/
enum ConnState {
    STATE_IDLE               = 0,  // 空闲，可执行任意命令
    STATE_SENDING_FILE       = 1,  // 正在发送文件（客户端间传输，等待 accept 或正在发送）
    STATE_RECEIVING_FILE     = 2,  // 正在接收文件（客户端间传输或下载）
    STATE_UPLOADING          = 3,  // 正在上传文件到服务端
    STATE_DOWNLOADING        = 4,  // 正在从服务端下载文件
    STATE_RELAYING_FILE      = 5,  // 【仅服务端】正在转发文件数据
    STATE_WAITING_FILE_CONFIRM = 6, // 【仅客户端】收到文件传输请求，等待用户 accept/reject
};

/************************ 统一连接上下文（客户端与服务端共用） ************************/
typedef struct ClientConn {
    int fd;                         // socket fd
    char id[CLIENT_ID_LEN];         // 客户端标识（客户端自存，服务端管理）
    bool running;                   // 线程运行标记
    pthread_t recv_tid;             // 接收线程ID
    pthread_t send_tid;             // 发送线程ID
    pthread_mutex_t send_mtx;       // 发送队列互斥锁
    pthread_mutex_t socket_mtx;     // socket写互斥锁
    enum ConnState state;           // 当前连接状态（业务层状态机）
    
    // 环形发送队列
    Packet* send_queue;
    int q_capacity;
    int q_head;
    int q_tail;
    // 链表指针（服务端管理器使用，客户端不使用）
    struct ClientConn* next;
} ClientConn;

/**
 * @brief 向发送队列入队数据包（线程安全）
 * @param conn 连接上下文
 * @param pkt 待发送数据包
 * @return 0成功，-1队列满/参数非法
 */
int client_send_enqueue(ClientConn* conn, Packet* pkt);
Packet* client_send_dequeue(ClientConn* conn);

/************************ Packet数据包内存管理接口 ************************/
/**
 * @brief 初始化空数据包，清零所有字段，body置空
 * @param pkt 待初始化Packet指针
 */
void packet_init(Packet* pkt);

/**
 * @brief 释放数据包内部动态包体内存，不会释放pkt本身
 * @param pkt 待释放body的数据包指针
 */
void packet_free_body(Packet* pkt);

/**
 * @brief 为Packet设置命令与包体数据，自动填充包头并转换网络字节序
 * @param pkt 目标数据包
 * @param pkt_type 数据包类型 PKT_TYPE
 * @param pkt_sub_type 当前类型数据包的细类类型  
 * @param body 包体原始数据，无包体传NULL
 * @param body_len_host 主机序包体长度
 * @return int 0成功，-1内存分配失败
 */
int packet_set_body(Packet* pkt, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type, const uint8_t* body, uint32_t body_len_host);

/************************ 底层TCP完整读写封装（解决半包粘包） ************************/
/**
 * @brief 阻塞循环读取指定长度字节，确保读取len个字节
 * @param fd socket文件描述符
 * @param buf 接收缓冲区
 * @param len 需要读取的总字节数
 * @return int 0读取成功，-1连接断开/IO错误
 */
int recv_n(int fd, void* buf, size_t len);

/**
 * @brief 阻塞循环发送指定长度字节，确保发送len个字节
 * @param fd socket文件描述符
 * @param buf 待发送数据缓冲区
 * @param len 需要发送的总字节数
 * @return int 0发送成功，-1连接断开/IO错误
 */
int send_n(int fd, const void* buf, size_t len);

/************************ 包头工具函数 ************************/
/**
 * @brief 填充包头结构体，自动将数值转为网络字节序
 * @param hdr 包头结构体指针
 * @param pkt_type 数据包类型
 * @param pkt_sub_type 当前类型数据包的细类类型  
 * @param body_len_host 主机序包体长度
 */
void pkt_header_init(PacketHeader* hdr, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type, uint32_t body_len_host);

/**
 * @brief 校验包头合法性：魔数、命令合法性、包体长度上限
 * @param hdr 待校验包头
 * @return int 返回0或对应负数错误码
 */
int pkt_header_check(const PacketHeader* hdr);

/************************ 数据包收发核心接口 ************************/

/**
 * @brief 一次性发送完整数据包（包头+包体），不等待应答，用于服务端推送消息
 * @param fd socket描述符
 * @param pkt 待发送数据包
 * @return int 0发送成功，-1发送失败
 */
int pkt_send_full(int fd, Packet* pkt);

/**
 * @brief 完整接收一个数据包，填充Packet结构体，内部自动malloc分配body
 * 流程：读取32字节包头 → 解析body_len → 读取对应长度包体
 * @param fd socket描述符
 * @param out_pkt 输出接收完成的数据包
 * @return int 0成功
 *            -1 IO读取失败/连接断开
 *            -2 包体malloc内存分配失败
 */
int pkt_recv_full(int fd, Packet* out_pkt);



#endif // TCP_PROTOCOL_H