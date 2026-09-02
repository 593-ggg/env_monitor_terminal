#ifndef TCP_CMD_PARSER_H
#define TCP_CMD_PARSER_H

#include "tcp_protocol.h"

/* 前向声明，避免与tcp_server.h循环包含 */
struct ClientConn;

/************************ 统一命令类型枚举（pkt_sub_type传输，客户端与服务端共用） ************************/
enum CMD_TYPE {
    CMD_UNKNOWN       = 0,    // 未知指令
    CMD_SET_ID        = 1,    // 设置客户端标识
    CMD_TXT           = 2,    // 普通文本消息
    CMD_MSG           = 3,    // 群聊消息
    CMD_MSG_PRIVATE   = 4,    // 私聊消息
    CMD_FILE_PUT      = 5,    // 文件传输（客户端之间）
    CMD_HEARTBEAT     = 6,    // 心跳
    CMD_CLIENT_LIST   = 7,    // 查询在线客户端列表
    CMD_FILE_ACCEPT   = 8,    // 接收方接受文件传输
    CMD_FILE_REJECT   = 9,    // 接收方拒绝文件传输
    CMD_FILE_UPLOAD   = 10,   // 上传文件到服务端
    CMD_FILE_DOWNLOAD = 11,   // 从服务端下载文件
    CMD_FILE_LIST     = 12,   // 列出服务端可下载的文件
    CMD_YOLOV8        = 13,   // YOLOv8 处理文件
    CMD_LED           = 14,   // 控制 LED 灯 (0-3 on/off)
    CMD_BEEP          = 15,   // 控制蜂鸣器 (on/off)
};

/************************ 服务端侧：PKT_MSG_TEXT包体 → 命令执行 ************************/

/**
 * @brief 服务端收到PKT_MSG_TEXT后，解析body并执行对应命令路由
 *        内部根据body格式映射到CLI_CMD，再分发到exec_cmd_xxx执行
 *
 *        body协议格式：
 *          "id:xxx"         → CMD_SET_ID   注册客户端标识
 *          "@target|msg"    → CMD_MSG_PRIVATE 私聊转发
 *          其他纯文本       → CMD_MSG       群发广播（排除发送者）
 *
 * @param conn 发送者连接上下文（tcp_server.h中定义）
 * @param pkt 输入数据包，包含body文本
 * @return 0成功处理，-1格式错误
 */
int cmd_server_dispatch(struct ClientConn* conn, Packet* pkt);

#endif // TCP_CMD_PARSER_H
