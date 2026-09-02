# 自定义 TCP 通信协议规范文档

> **重要说明**：本文档已与 `src/network/` 实际实现完全对齐。早期 V2/V3 版本描述的"两步握手 + CMD_RESPONSE 应答包"模型已废弃，实际协议采用**一次性完整发包**模型。如文档与源码再次出现不一致，**以源码 `tcp_protocol.h` 为准**并同步更新本文档。
>
> 对应源码：
> - `src/network/tcp_protocol.{h,c}` —— 底层协议库
> - `src/network/tcp_server.{h,c}` —— 服务端核心（客户端管理器 + 文件会话）
> - `src/network/tcp_cmd_parser.{h,c}` —— 服务端命令路由
> - `tcp_client/tcp_cmd_parser.c` —— 客户端命令解析（独立实现）

## 版本演进
- V1/V2：两步握手（先发包头 → 服务端应答 → 客户端发 body）
- **当前版本**：废弃两步握手，改用一次性完整收发 `pkt_send_full` / `pkt_recv_full`，简化时序、降低 RTT
- 命令枚举拆分为 **两层**：`PKT_TYPE`（包头 `pkt_type` 字段）+ `CMD_TYPE`（包头 `pkt_sub_type` 字段，业务命令子类型）
- 错误应答改用 `PKT_ERR` 包，错误子类型由 `ERR_TYPE` 枚举描述

## 适用场景
C/C++ Linux TCP 通信，应用于 RK3568 嵌入式终端：
- 终端之间文本消息（群聊 / 私聊）
- 客户端标识注册、在线列表查询
- 客户端之间文件传输（P2P 经服务端中转）
- 客户端上传文件到服务端、从服务端下载文件
- 服务端文件列表查询
- 远程 YOLOv8 推理请求
- 远程 LED / 蜂鸣器控制
- 心跳保活

---

# 1 基础传输约定

## 1.1 传输层与字节序
1. 底层：TCP 字节流，全部二进制传输
2. 包头内部所有数值字段统一**网络大端字节序**（`htobe32` / `htobe16`）
3. 字符串类包体为 UTF-8 裸字节流，**不带 `\0` 结束符**，长度由 `body_len` 精确控制

## 1.2 数据包交互模型（**当前版本**）
**一次性完整收发**，无两步握手：
- 发送方：调用 `pkt_send_full` 一次性发送「32 字节包头 + body_len 字节包体」
- 接收方：调用 `pkt_recv_full` 先读 32 字节包头，再按 `body_len` 读取完整包体
- 业务应答通过 `PKT_MSG_TEXT`（普通文本提示）或 `PKT_ERR`（错误应答）回传，**不再有专门的 `CMD_RESPONSE` 包**

## 1.3 强制底层工具依赖
必须实现可靠读写封装 `recv_n` / `send_n`：循环读写，保证读取/发送指定长度字节；读写失败、连接断开立即返回错误。
- `recv_n` 返回 0 成功，-1 失败（含对端正常关闭 `ret==0` 转换为 -1）
- `send_n` 使用 `MSG_NOSIGNAL` 避免 SIGPIPE 杀进程，被信号中断自动重试

## 1.4 全局协议宏（`tcp_protocol.h`，固定不可修改）
```c
#define PACKET_MAGIC     0xAA55CCDDU        // 数据包校验魔数
#define HEADER_SIZE      32U                // 包头固定字节长度
#define MAX_BODY_SIZE    (512 * 1024U)      // 单包最大包体 512KB（注意：非 8MB）
#define FILE_CHUNK_SIZE  (256 * 1024U)      // 文件分片固定 256KB（注意：非 8KB）
#define CLIENT_ID_LEN    64                 // 客户端标识最大长度
#define SEND_QUEUE_CAP   32                 // 单连接发送队列容量
```

> **历史版本差异提示**：早期文档将 `MAX_BODY_SIZE` 标为 8MB、`FILE_CHUNK_SIZE` 标为 8KB，**与实现不符**。调试时以 `512KB` / `256KB` 为准。

## 1.5 服务端配置宏（`tcp_server.h`）
```c
#define SERVER_LISTEN_PORT 8888   // 监听端口（可被 AppConfig.server_port 覆盖）
#define SERVER_BACKLOG     10     // listen 队列长度
#define CLIENT_RECV_BUF_LEN 4096  // 客户端接收缓冲区
#define MAX_CLIENTS        64     // 最大连接数
```

---

# 2 二进制数据包结构定义

## 2.1 包头 `PacketHeader`（固定 32 字节，单字节对齐）

```c
#pragma pack(1)
typedef struct {
    uint32_t magic;        // 魔数，网络字节序，固定 0xAA55CCDD
    uint16_t pkt_type;     // 数据包类型 PKT_TYPE，网络字节序
    uint32_t body_len;     // 后续包体字节长度，网络字节序
    uint8_t  pkt_sub_type; // 子类型（PKT_MSG_TEXT 时为 CMD_TYPE；PKT_ERR 时为 ERR_TYPE；其他类型为 0）
    uint64_t reserved;     // 预留 8 字节扩展字段（当前未使用，置 0）
    uint8_t  reserve[13];  // 填充字节，保证结构体总大小严格 32 字节
} PacketHeader;
#pragma pack()
```

> **关键变更**：相对早期文档，新增了 `pkt_sub_type`（1 字节）字段，`reserve` 数量从 14 → 13。该字段承载业务命令子类型，是**当前协议的核心扩展点**。

字段大小校验：`4 + 2 + 4 + 1 + 8 + 13 = 32` 字节 ✓

## 2.2 统一数据包封装 `Packet`
业务层统一使用该结构体管理包头与动态包体，隔离内存管理逻辑：
```c
typedef struct {
    PacketHeader hdr;       // 固定 32 字节包头
    uint8_t*     body;      // 动态分配包体缓冲区，body_len=0 时为 NULL
    uint32_t     body_len;  // 主机序包体有效长度，缓存减少频繁字节序转换
} Packet;
```

## 2.3 数据包类型枚举 `PKT_TYPE`（`pkt_type` 字段）
```c
enum PKT_TYPE {
    PKT_INVALID   = 0,  // 非法数据包
    PKT_MSG_TEXT  = 1,  // 文本承载包（所有聊天 / 管理指令 / 文本应答都放该包 body）
    PKT_FILE_META = 2,  // 文件元信息包（客户端互传 / 上传 / 下载共用）
    PKT_FILE_DATA = 3,  // 文件二进制分片包
    PKT_FILE_END  = 4,  // 文件传输结束标记包
    PKT_HEARTBEAT = 5,  // 心跳保活包
    PKT_ERR       = 6   // 错误应答包（body 携带错误描述，pkt_sub_type 携带 ERR_TYPE）
};
```

> **历史差异**：早期文档将应答包命名为 `CMD_RESPONSE = 100`，**已废弃**。当前实现用 `PKT_ERR = 6` 表示错误应答，普通文本提示用 `PKT_MSG_TEXT`。

## 2.4 业务命令枚举 `CMD_TYPE`（`pkt_sub_type` 字段，仅当 `pkt_type == PKT_MSG_TEXT` 时有效）
```c
enum CMD_TYPE {
    CMD_UNKNOWN       = 0,   // 未知指令
    CMD_SET_ID        = 1,   // 设置客户端标识
    CMD_TXT           = 2,   // 普通文本消息（服务端文本提示，如下发 "id set ok"）
    CMD_MSG           = 3,   // 群聊消息（广播，排除发送者）
    CMD_MSG_PRIVATE   = 4,   // 私聊消息（点对点转发）
    CMD_FILE_PUT      = 5,   // 客户端之间文件传输
    CMD_HEARTBEAT     = 6,   // 心跳
    CMD_CLIENT_LIST   = 7,   // 查询在线客户端列表
    CMD_FILE_ACCEPT   = 8,   // 接收方接受文件传输
    CMD_FILE_REJECT   = 9,   // 接收方拒绝文件传输
    CMD_FILE_UPLOAD   = 10,  // 上传文件到服务端
    CMD_FILE_DOWNLOAD = 11,  // 从服务端下载文件
    CMD_FILE_LIST     = 12,  // 列出服务端可下载文件
    CMD_YOLOV8        = 13,  // 远程 YOLOv8 推理请求
    CMD_LED           = 14,  // 控制 LED（0-3 on/off）
    CMD_BEEP          = 15,  // 控制蜂鸣器（on/off）
};
```

## 2.5 错误类型枚举 `ERR_TYPE`（`pkt_sub_type` 字段，仅当 `pkt_type == PKT_ERR` 时有效）
```c
enum ERR_TYPE {
    ERR_GENERAL          = 0,  // 通用错误
    ERR_FILE_NOT_FOUND   = 1,  // 文件不存在
    ERR_FILE_OPEN_FAILED = 2,  // 文件打开失败
    ERR_FILE_WRITE_FAILED = 3, // 文件写入失败
    ERR_BUSY             = 4,  // 服务端繁忙（如文件会话占用）
    ERR_PERMISSION       = 5,  // 权限不足
};
```

## 2.6 包头校验错误码 `RESP_CODE`（`pkt_header_check` 返回值）
```c
#define RESP_OK             0    // 包头校验通过
#define RESP_ERR_MAGIC      -1   // 魔数不匹配
#define RESP_ERR_CMD        -2   // pkt_type 非法（不在 PKT_INVALID+1 ~ PKT_HEARTBEAT 范围）
#define RESP_ERR_BODY_LIMIT -3   // body_len 超过 MAX_BODY_SIZE (512KB)
```

> **历史差异**：早期文档包含 `RESP_ERR_INTERNAL(-4)` / `RESP_ERR_FILE(-5)`，**当前实现已移除**。业务错误统一通过 `PKT_ERR` 包 + `ERR_TYPE` 子类型回传。

## 2.7 连接状态枚 `ConnState`（业务层状态机，存于 `ClientConn.state`）
```c
enum ConnState {
    STATE_IDLE                = 0,  // 空闲，可执行任意命令
    STATE_SENDING_FILE        = 1,  // 正在发送文件（客户端互传，等待 accept 或正在发送）
    STATE_RECEIVING_FILE      = 2,  // 正在接收文件（客户端互传或下载）
    STATE_UPLOADING           = 3,  // 正在上传文件到服务端
    STATE_DOWNLOADING         = 4,  // 正在从服务端下载文件
    STATE_RELAYING_FILE       = 5,  // 【仅服务端】正在转发文件数据
    STATE_WAITING_FILE_CONFIRM = 6, // 【仅客户端】收到文件传输请求，等待用户 accept/reject
};
```

## 2.8 统一连接上下文 `ClientConn`（客户端与服务端共用）
```c
typedef struct ClientConn {
    int fd;                          // socket fd
    char id[CLIENT_ID_LEN];          // 客户端标识
    char client_ip[32];              // 客户端 IP（如 "192.168.1.100"）
    uint16_t client_port;            // 客户端端口（主机序）
    bool running;                    // 线程运行标记
    pthread_t recv_tid;              // 接收线程 ID
    pthread_t send_tid;              // 发送线程 ID
    pthread_mutex_t send_mtx;        // 发送队列互斥锁
    pthread_mutex_t socket_mtx;      // socket 写互斥锁
    enum ConnState state;            // 当前连接状态
    Packet* send_queue;              // 环形发送队列（容量 SEND_QUEUE_CAP）
    int q_capacity;                  // 队列容量
    int q_head;                      // 队列头索引
    int q_tail;                      // 队列尾索引
    struct ClientConn* next;         // 链表指针（服务端管理器使用，客户端不用）
} ClientConn;
```

---

# 3 各命令 Body 二进制格式规范

## 3.1 `PKT_MSG_TEXT` 文本承载包
`pkt_sub_type` 区分具体业务命令，body 格式按命令不同：

| `pkt_sub_type` | 命令 | body 格式 | 示例 | 方向 |
|---|---|---|---|---|
| `CMD_SET_ID` (1) | 注册 ID | `id字符串` | `alice` | C → S |
| `CMD_TXT` (2) | 普通文本提示 | `任意文本` | `id set ok: alice` | S → C |
| `CMD_MSG` (3) | 群聊 | `纯文本消息` | `hello everyone` | C → S → 其他 C |
| `CMD_MSG_PRIVATE` (4) | 私聊 | `@target\|msg`（C→S）；`[sender]msg`（S→C 转发） | `@bob\|晚上吃饭` / `[alice]晚上吃饭` | C → S → 目标 C |
| `CMD_FILE_PUT` (5) | 客户端互传请求 | `@target\|filename\|filesize` | `@bob\|demo.png\|2048000` | C → S |
| `CMD_FILE_ACCEPT` (8) | 接受传输 | `@sender` | `@alice` | C → S |
| `CMD_FILE_REJECT` (9) | 拒绝传输 | `@sender` | `@alice` | C → S |
| `CMD_CLIENT_LIST` (7) | 查询在线列表 | 无 body（`body_len=0`） | — | C → S |
| `CMD_FILE_UPLOAD` (10) | 上传到服务端 | `filename\|filesize` | `report.txt\|1024` | C → S |
| `CMD_FILE_DOWNLOAD` (11) | 从服务端下载 | `filename` | `report.txt` | C → S |
| `CMD_FILE_LIST` (12) | 列出服务端文件 | 无 body | — | C → S |
| `CMD_YOLOV8` (13) | 远程推理 | `input_path\|output_path` | `/tmp/in.jpg\|/tmp/out.jpg` | C → S |
| `CMD_LED` (14) | LED 控制 | `led_index on_off` | `0 on` / `3 off` | C → S |
| `CMD_BEEP` (15) | 蜂鸣器控制 | `on_off` | `on` / `off` | C → S |

## 3.2 `PKT_FILE_META` 文件元信息包
- 服务端用 `server_file_session_set` / `server_upload_session_set` 建立会话后，向接收端推送元信息
- body 格式：`sender_id|filename|filesize`（与 `CMD_FILE_PUT` 类似，由服务端转发时构造）

## 3.3 `PKT_FILE_DATA` 文件分片数据
- body = 文件原始二进制分片
- 单分片最大 `FILE_CHUNK_SIZE = 256KB`
- `pkt_sub_type` 通常为 0

## 3.4 `PKT_FILE_END` 文件传输结束标记
- `body_len = 0`，无包体
- 服务端 / 客户端收到后关闭文件句柄、清理会话、状态机回 `STATE_IDLE`

## 3.5 `PKT_HEARTBEAT` 心跳包
- `body_len = 0`，无包体

## 3.6 `PKT_ERR` 错误应答包
- `pkt_type = PKT_ERR (6)`
- `pkt_sub_type = ERR_TYPE` 错误类型枚举值
- body = 错误描述字符串（UTF-8，无 `\0`）
- 示例：`pkt_sub_type=ERR_FILE_NOT_FOUND(1)`，body=`file not found: report.txt`

---

# 4 模块分层架构

## 4.1 模块拆分边界（单一职责）

### 模块 1：`tcp_protocol.{h,c}` —— 底层纯 TCP 协议库
**仅包含纯协议、二进制收发、包头校验、Packet 内存管理、连接上下文与发送队列，无任何业务命令逻辑**

对外提供能力：
1. `recv_n` / `send_n`：可靠完整读写底层 socket
2. `packet_init` / `packet_free_body` / `packet_set_body`：Packet 内存管理
3. `pkt_header_init` / `pkt_header_check`：包头初始化与三重校验（魔数 / 类型 / 长度上限）
4. `pkt_send_full`：一次性发送完整数据包（包头 + 包体），**不等待应答**
5. `pkt_recv_full`：完整接收一个数据包（先读 32 字节包头，再按 `body_len` 读包体）
6. `client_send_enqueue` / `client_send_dequeue`：线程安全环形发送队列

> **历史差异**：早期文档提到的 `pkt_send_biz` / `pkt_send_response` / `pkt_parse_response_body` / `ResponseData` **已全部废弃**，当前实现不再有两步握手与应答包解析结构体。

### 模块 2：`tcp_server.{h,c}` —— 服务端核心
**实现 accept 循环、客户端管理器（链表 + 互斥锁）、文件传输会话表、广播 / 私聊、UI 查询接口**

对外提供能力：
1. `server_start` / `server_shutdown`：服务端启动 / 关闭
2. `client_mgr_init` / `client_mgr_add` / `client_mgr_remove` / `client_mgr_set_id`：客户端管理
3. `client_mgr_find_by_id` / `client_mgr_broadcast` / `client_mgr_send_to_id`：查找与发送
4. `client_mgr_count` / `client_mgr_list` / `client_mgr_list_to` / `client_mgr_disconnect`：查询与断开
5. `client_mgr_get_info_list`：UI 查询在线客户端信息（线程安全拷贝）
6. `server_file_session_set` / `server_upload_session_set`：文件会话管理

### 模块 3：`tcp_cmd_parser.{h,c}` —— 服务端命令路由
**解析 `PKT_MSG_TEXT` 包体，按 `pkt_sub_type`（CMD_TYPE）分发到 `exec_cmd_xxx` 执行**

对外提供能力：
- `cmd_server_dispatch(ClientConn* conn, Packet* pkt)`：唯一入口

内部静态分发函数（仅模块内可见）：
- `exec_cmd_set_id` / `exec_cmd_private_msg` / `exec_cmd_broadcast`
- `exec_cmd_file_put` / `exec_cmd_file_accept` / `exec_cmd_file_reject`
- `exec_cmd_file_upload` / `exec_cmd_file_download` / `exec_cmd_file_list`
- `exec_cmd_yolov8` / `exec_cmd_led` / `exec_cmd_beep`
- `server_yolov8_process` / `process_thread_func`：远程 YOLOv8 异步处理
- `scan_dir_recursive`：文件列表递归扫描
- `send_text_to` / `send_error_to`：发送文本 / 错误应答

### 模块 4：`tcp_client/tcp_cmd_parser.c` —— 客户端命令解析（独立实现）
**解析终端 CLI 字符串 → 构造 `Packet`，与服务端命令路由解耦**

对外提供能力：
- `packet_from_cmd(const char* cmdline, Packet* out_pkt)`：唯一入口

内部静态解析函数（13 条命令，无 `/` 前缀）：
- `parse_cmd_set_id` / `parse_cmd_msg` / `parse_cmd_msg_private`
- `parse_cmd_file_put` / `parse_cmd_file_accept` / `parse_cmd_file_reject`
- `parse_cmd_heartbeat` / `parse_cmd_file_upload` / `parse_cmd_file_download`
- `parse_cmd_file_list`（CLI 关键字 `files`）/ `parse_cmd_yolov8` / `parse_cmd_led` / `parse_cmd_beep`
- `parse_cli_cmd`：CLI 关键字 → `CMD_TYPE` 映射

---

# 5 标准交互时序规范

## 5.1 通用数据包收发（所有命令统一）
**当前版本无两步握手**，时序如下：

### 发送方（客户端或服务端）
1. 调用 `packet_set_body(&pkt, pkt_type, pkt_sub_type, body, body_len)` 填充 Packet（自动完成包头网络字节序转换 + body malloc 拷贝）
2. 调用 `client_send_enqueue(conn, &pkt)` 入队发送队列（线程安全，**会再次拷贝一份 body**，调用方随后需 `packet_free_body`）
3. 发送线程从队列 `client_send_dequeue` 取出，调用 `pkt_send_full(conn->fd, &pkt)` 一次性发送包头 + 包体

> 服务端主动推送时可直接构造 Packet 后调用 `client_send_enqueue`，无需等待应答。

### 接收方
1. 调用 `pkt_recv_full(fd, &pkt)`：内部先 `recv_n` 读 32 字节包头 → `pkt_header_check` 校验 → 按 `body_len` `malloc` + `recv_n` 读包体
2. 根据 `pkt.hdr.pkt_type` 分发：
   - `PKT_MSG_TEXT` → `cmd_server_dispatch`（服务端）或客户端业务处理
   - `PKT_FILE_META` / `PKT_FILE_DATA` / `PKT_FILE_END` → 文件会话处理
   - `PKT_HEARTBEAT` → 心跳更新
   - `PKT_ERR` → 错误展示
3. 处理完毕 `packet_free_body(&pkt)` 释放 body

## 5.2 业务应答机制
**当前版本无 `CMD_RESPONSE` 包**，应答分两类：
- **普通文本提示**：服务端用 `send_text_to(conn, text)` 发送 `PKT_MSG_TEXT` + `CMD_TXT`，如 `id set ok: alice`、`user 'bob' not online`
- **错误应答**：服务端用 `send_error_to(conn, err_type, err_msg)` 发送 `PKT_ERR` + `ERR_TYPE` 子类型，如 `ERR_FILE_NOT_FOUND` + `file not found: xxx`

## 5.3 状态机保护
服务端 `cmd_server_dispatch` 对文件类命令（`CMD_FILE_PUT` / `CMD_FILE_UPLOAD` / `CMD_FILE_DOWNLOAD`）做状态保护：仅 `STATE_IDLE` 状态允许执行，否则回复 `busy, current state: N` 文本。

---

# 6 典型业务完整交互时序

## 时序 A：客户端注册 ID
```
C → S: PKT_MSG_TEXT / CMD_SET_ID / body="alice"
S: client_mgr_set_id 校验唯一性
S → C: PKT_MSG_TEXT / CMD_TXT / body="id set ok: alice"   （成功）
S → C: PKT_MSG_TEXT / CMD_TXT / body="id 'alice' already in use"  （失败）
```

## 时序 B：私聊消息
```
C1 → S: PKT_MSG_TEXT / CMD_MSG_PRIVATE / body="@bob|晚上吃饭"
S: 解析 target=bob，client_mgr_find_by_id
  成功：
S → C2: PKT_MSG_TEXT / CMD_MSG_PRIVATE / body="[alice]晚上吃饭"
  失败：
S → C1: PKT_MSG_TEXT / CMD_TXT / body="user 'bob' not online"
```

## 时序 C：群聊广播
```
C1 → S: PKT_MSG_TEXT / CMD_MSG / body="hello everyone"
S: client_mgr_broadcast（排除 C1 的 fd）
S → C2/C3/...: PKT_MSG_TEXT / CMD_MSG / body="hello everyone"
```

## 时序 D：客户端之间文件传输（P2P 经服务端中转）
```
C1 → S: PKT_MSG_TEXT / CMD_FILE_PUT / body="@bob|demo.png|2048000"
S: 查找 bob，设置 server_file_session_set(C1.fd, bob_conn)
S → C2: PKT_FILE_META / body="alice|demo.png|2048000"   （服务端转发元信息）
C2 → S: PKT_MSG_TEXT / CMD_FILE_ACCEPT / body="@alice"   （或 CMD_FILE_REJECT）
S → C1: PKT_MSG_TEXT / CMD_TXT / body="accept" / "reject"
C1 循环：
C1 → S: PKT_FILE_DATA / body=<256KB 二进制>
S: 查会话，转发给 C2
S → C2: PKT_FILE_DATA / body=<同上>
C1 → S: PKT_FILE_END
S → C2: PKT_FILE_END
S: 清理会话，双方状态回 STATE_IDLE
```

## 时序 E：上传文件到服务端
```
C → S: PKT_MSG_TEXT / CMD_FILE_UPLOAD / body="report.txt|1024"
S: server_upload_session_set(C.fd, file_fd, "report.txt")，状态 STATE_UPLOADING
S → C: PKT_MSG_TEXT / CMD_TXT / body="upload ready"
C 循环：
C → S: PKT_FILE_DATA / body=<256KB>
S: 写入 file_fd
C → S: PKT_FILE_END
S: 关闭 file_fd，状态回 STATE_IDLE
S → C: PKT_MSG_TEXT / CMD_TXT / body="upload done"
```

## 时序 F：从服务端下载文件
```
C → S: PKT_MSG_TEXT / CMD_FILE_DOWNLOAD / body="report.txt"
S: 打开文件，设置会话，状态 STATE_DOWNLOADING
S → C: PKT_FILE_META / body="server|report.txt|1024"
S 循环读文件：
S → C: PKT_FILE_DATA / body=<256KB>
S → C: PKT_FILE_END
S: 状态回 STATE_IDLE
```

## 时序 G：远程 YOLOv8 推理
```
C → S: PKT_MSG_TEXT / CMD_YOLOV8 / body="/tmp/in.jpg|/tmp/out.jpg"
S: 起后台线程 process_thread_func 执行 server_yolov8_process
S → C: PKT_MSG_TEXT / CMD_TXT / body="yolov8 done: /tmp/out.jpg"   （完成）
或 S → C: PKT_ERR / ERR_GENERAL / body="yolov8 failed: model not init"
```

## 时序 H：远程 LED / 蜂鸣器控制
```
C → S: PKT_MSG_TEXT / CMD_LED / body="0 on"
S: device_led_on(0)
S → C: PKT_MSG_TEXT / CMD_TXT / body="led 0 on ok"
```

## 时序 I：包头校验失败
接收方 `pkt_recv_full` 读包头后 `pkt_header_check` 返回非 0：
- 当前实现**不主动回发错误包**，直接丢弃该连接的后续数据（依赖 `recv_n` 失败关闭连接）
- 调试时观察服务端 `printf("pkt_type_host: %d\n", ...)` 日志

---

# 7 异常、超时与内存安全规范

## 7.1 读写异常处理
- `recv_n` 返回 0 成功，-1 失败（含对端正常关闭 `ret==0`、系统错误、非 EINTR 信号）
- `send_n` 返回 0 成功，-1 失败（使用 `MSG_NOSIGNAL`）
- 任何 IO 失败：上层必须关闭 socket、释放 Packet 包体内存、回收线程

## 7.2 内存管理强制规则
1. `Packet` 使用前必须 `packet_init`
2. 所有 Packet 使用完毕必须 `packet_free_body` 释放动态 body 缓冲区
3. `packet_set_body` 内部会自动 `packet_free_body` 释放原有 body，上层无需重复释放
4. `pkt_recv_full` 内部 `malloc(body_len)` 分配 body，**调用方负责 `packet_free_body`**
5. `client_send_enqueue` 入队时**再次拷贝 body**，调用方 enqueue 后仍需 `packet_free_body` 自身的 pkt
6. `client_send_dequeue` 返回队列内 Packet 指针，发送完成后由队列槽位复用，**外部不要 free**

## 7.3 内存分配失败
- `packet_set_body` malloc 失败返回 -1
- `pkt_recv_full` malloc body 失败返回 -2（包头已读但 body 无法分配，连接数据流已乱，应关闭连接）

## 7.4 发送队列保护
- 队列容量 `SEND_QUEUE_CAP = 32`，环形队列满时 `client_send_enqueue` 返回 -1
- 慢消费者会导致发送方 enqueue 失败，业务层应处理（当前实现仅 printf）
- 队列无超时机制，调试时注意慢消费者导致消息丢失

## 7.5 缓冲区溢出防护
- `cmd_server_dispatch` 内部 `char buf[2048]`，body 拷贝时按 `sizeof(buf)-1` 截断
- 所有字符串拼接使用定长数组 + `snprintf` 截断

## 7.6 关机保护
- `g_server_shutting_down` 全局标记，`server_shutdown` 时置 true
- 防止下线广播风暴与 double free
- 调试时注意：关机后任何 `client_mgr_*` 调用都未定义

---

# 8 终端命令解析规则（`tcp_client/tcp_cmd_parser.c` 专属）

支持 13 条 CLI 指令，**无 `/` 前缀**（早期文档的 `/msg` `/file_put` `/heartbeat` 已废弃），入口 `packet_from_cmd`：

| CLI 输入 | 映射 CMD_TYPE | body 格式 | 说明 |
|---|---|---|---|
| `id alice` | `CMD_SET_ID` | `alice` | 注册客户端标识 |
| `msg hello` | `CMD_MSG` | `hello` | 群聊广播 |
| `msg @bob hi` | `CMD_MSG_PRIVATE` | `@bob\|hi` | 私聊（`@` 开头触发私聊） |
| `file @bob /path/demo.png` | `CMD_FILE_PUT` | `@bob\|demo.png\|<size>` | 客户端互传（自动剥离路径、stat 取 size） |
| `file_accept @alice` | `CMD_FILE_ACCEPT` | `@alice` | 接受文件传输 |
| `file_reject @alice` | `CMD_FILE_REJECT` | `@alice` | 拒绝文件传输 |
| `heartbeat` | `CMD_HEARTBEAT` | 无 body | 心跳 |
| `list` | `CMD_CLIENT_LIST` | 无 body | 查询在线列表 |
| `upload /path/report.txt` | `CMD_FILE_UPLOAD` | `report.txt\|<size>` | 上传到服务端 |
| `download report.txt` | `CMD_FILE_DOWNLOAD` | `report.txt` | 从服务端下载 |
| `files` | `CMD_FILE_LIST` | 无 body | 列出服务端文件 |
| `yolov8 /tmp/in.jpg /tmp/out.jpg` | `CMD_YOLOV8` | `/tmp/in.jpg\|/tmp/out.jpg` | 远程推理 |
| `led 0 on` / `led 3 off` | `CMD_LED` | `0 on` / `3 off` | LED 控制 |
| `beep on` / `beep off` | `CMD_BEEP` | `on` / `off` | 蜂鸣器控制 |

未匹配任何关键字时返回 `CMD_UNKNOWN`。

---

# 9 对外 API 总览

## 9.1 `tcp_protocol` 底层协议 API

### Packet 内存管理
```c
void packet_init(Packet* pkt);
void packet_free_body(Packet* pkt);
int  packet_set_body(Packet* pkt, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type,
                     const uint8_t* body, uint32_t body_len_host);
```

### 底层可靠读写
```c
int recv_n(int fd, void* buf, size_t len);     // 0 成功，-1 失败
int send_n(int fd, const void* buf, size_t len); // 0 成功，-1 失败
```

### 包头工具
```c
void pkt_header_init(PacketHeader* hdr, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type,
                     uint32_t body_len_host);
int  pkt_header_check(const PacketHeader* hdr);  // 返回 RESP_OK / RESP_ERR_*
```

### 数据包收发核心
```c
int pkt_send_full(int fd, Packet* pkt);   // 一次性发送包头+包体，0 成功，-1 失败
int pkt_recv_full(int fd, Packet* out_pkt); // 0 成功，-1 IO 失败，-2 body malloc 失败
```

### 发送队列（线程安全）
```c
int       client_send_enqueue(ClientConn* conn, Packet* pkt); // 0 成功，-1 队列满/参数非法
Packet*   client_send_dequeue(ClientConn* conn);              // 返回 Packet 指针，空队列返回 NULL
```

## 9.2 `tcp_server` 服务端核心 API

### 服务端生命周期
```c
void server_start(void);
void server_shutdown(void);
void client_conn_destroy(ClientConn* conn);
```

### 客户端管理器（线程安全）
```c
void   client_mgr_init(void);
int    client_mgr_add(ClientConn* conn);
void   client_mgr_remove(ClientConn* conn);
int    client_mgr_set_id(ClientConn* conn, const char* id);
ClientConn* client_mgr_find_by_id(const char* id);
int    client_mgr_broadcast(Packet* pkt, int exclude_fd);
int    client_mgr_send_to_id(const char* id, Packet* pkt);
int    client_mgr_count(void);
void   client_mgr_list(void);
void   client_mgr_list_to(ClientConn* conn);
void   client_mgr_disconnect(const char* id);
```

### UI 查询接口
```c
typedef struct { char id[64]; char ip[32]; uint16_t port; } ClientInfo;
int client_mgr_get_info_list(ClientInfo* out_buf, int max_count);
```

### 文件会话管理
```c
void server_file_session_set(int sender_fd, ClientConn* target);
void server_upload_session_set(int uploader_fd, int file_fd, const char* filename);
```

## 9.3 `tcp_cmd_parser` 服务端命令路由 API
```c
int cmd_server_dispatch(ClientConn* conn, Packet* pkt);  // 0 成功，-1 格式错误
```

## 9.4 客户端命令解析 API（`tcp_client/`）
```c
int packet_from_cmd(const char* cmdline, Packet* out_pkt);  // 0 成功，-1 解析失败
```

---

# 10 扩展规范

## 10.1 新增二进制命令（非文本类）
1. 在 `enum PKT_TYPE` 增加枚举（值递增，勿跳号）
2. 同步更新 `pkt_header_check` 的合法性范围校验（当前 `pkt_type > PKT_HEARTBEAT` 即非法，需调整）
3. 定义对应 body 格式
4. 上层手动调用 `packet_set_body` 构造 Packet 发包

## 10.2 新增文本业务命令（PKT_MSG_TEXT 子命令）
1. 在 `enum CMD_TYPE` 增加枚举
2. 服务端 `tcp_cmd_parser.c` 新增 `static int exec_cmd_xxx`，并在 `cmd_server_dispatch` 的 `switch` 增加分支
3. 客户端 `tcp_client/tcp_cmd_parser.c` 新增 `parse_cmd_xxx`，并在 `parse_cli_cmd` 增加关键字匹配
4. **两端 CMD_TYPE 枚举值必须严格一致**（共用 `tcp_protocol.h`，但客户端 `tcp_cmd_parser.c` 单独定义，需手动同步）

## 10.3 新增错误类型
1. 在 `enum ERR_TYPE` 增加枚举
2. 服务端用 `send_error_to(conn, NEW_ERR_TYPE, msg)` 发送

## 10.4 包头扩展
- 仅允许使用 `reserved`（8 字节）/ `reserve[13]` 预留字段
- **禁止修改现有字段顺序、长度**，否则破坏向后兼容
- 修改 `MAX_BODY_SIZE` / `FILE_CHUNK_SIZE` 必须两端同步

## 10.5 注意事项
- `pkt_sub_type` 是 1 字节 `uint8_t`，CMD_TYPE / ERR_TYPE 枚举值不可超过 255
- `pkt_header_check` 当前校验范围 `pkt_type <= PKT_INVALID || pkt_type > PKT_HEARTBEAT`，**新增 PKT_TYPE 时务必放宽上限**（如改为 `> PKT_ERR`）
- 客户端 `tcp_cmd_parser.c` 的 `CMD_TYPE` 枚举是**独立复制**的，修改服务端枚举后必须手动同步客户端

---

# 附录 A：与历史版本（V2/V3）的差异速查

| 维度 | V2/V3 文档（已废弃） | 当前实现 |
|---|---|---|
| 交互模型 | 两步握手（先包头 → 应答 → 再 body） | 一次性完整收发 |
| 应答包 | `CMD_RESPONSE = 100` + `ResponseData` 结构体 | `PKT_ERR = 6` + `ERR_TYPE` 子类型；普通文本用 `PKT_MSG_TEXT + CMD_TXT` |
| 包头字段 | 无 `pkt_sub_type` | 新增 `pkt_sub_type`（1 字节），`reserve` 14 → 13 |
| 枚举命名 | `CMD_MSG_SEND` / `CMD_FILE_PUT_META` | `PKT_MSG_TEXT` / `PKT_FILE_META` |
| 枚举层级 | 单层 `CMD_TYPE` | 两层：`PKT_TYPE`（包头）+ `CMD_TYPE`（子类型）+ `ERR_TYPE`（错误子类型） |
| MAX_BODY_SIZE | 8 MB | 512 KB |
| FILE_CHUNK_SIZE | 8 KB | 256 KB |
| RESP_TIMEOUT_MS | 3000 ms | 无（无两步握手） |
| 错误码 | 6 个（含 INTERNAL / FILE） | 4 个包头校验码 + 6 个 ERR_TYPE 业务错误 |
| 命令数量 | 3 条（/msg /file_put /heartbeat） | 13 条（id/msg/file/file_accept/file_reject/heartbeat/list/upload/download/files/yolov8/led/beep） |
| CLI 前缀 | `/` | 无前缀 |
| 发送 API | `pkt_send_biz` + `pkt_send_response` | `pkt_send_full` + `client_send_enqueue` |
| 解析 API | `packet_from_cmdline` | `packet_from_cmd`（客户端） |
| `packet_set_body` 签名 | 4 参数（无 sub_type） | 5 参数（含 `pkt_sub_type`） |
| 连接上下文 | 未定义 | `ClientConn` 结构体 + 环形发送队列 |
| 状态机 | 未定义 | `ConnState` 7 个状态 |
| 模块拆分 | tcp_protocol + tcp_cmd_parser | tcp_protocol + tcp_server + tcp_cmd_parser + tcp_client/tcp_cmd_parser |
