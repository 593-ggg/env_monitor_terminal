#ifndef TCP_CMD_PARSER_H
#define TCP_CMD_PARSER_H

#include "tcp_protocol.h"

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


/************************ 客户端侧：终端命令 → Packet ************************/

/**
 * @brief 解析终端整行命令，分发到对应命令处理函数生成Packet
 * @param cmdline 终端原始输入字符串
 * @param out_pkt 输出数据包，用完调用packet_free_body释放body
 * @return 0成功
 *        -1 命令格式错误
 *        -2 内存分配失败
 *        -3 文件操作失败(stat/路径非法)
 */
int packet_from_cmd(const char* cmdline, Packet* out_pkt);

#endif // TCP_CMD_PARSER_H
