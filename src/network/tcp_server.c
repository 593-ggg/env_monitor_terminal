#include "tcp_server.h"
#include "tcp_cmd_parser.h"
#include "config_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 全局关机标记（新增）
bool g_server_shutting_down = false;

/************************ 文件传输会话管理 ************************/
#define MAX_FILE_SESSIONS 16
static struct
{
    int sender_fd;      // 发送者fd
    ClientConn* target; // 接收者连接
    bool active;
} g_file_sessions[MAX_FILE_SESSIONS];
static pthread_mutex_t g_file_session_mtx = PTHREAD_MUTEX_INITIALIZER;
// 上传会话管理（客户端上传文件到服务端）
#define MAX_UPLOAD_SESSIONS 8
static struct
{
    int uploader_fd; // 上传者fd
    int file_fd;     // 服务端文件描述符
    char filename[256];
    uint64_t received;
    bool active;
} g_upload_sessions[MAX_UPLOAD_SESSIONS];
static pthread_mutex_t g_upload_session_mtx = PTHREAD_MUTEX_INITIALIZER;
// 初始化 session 数组
static void server_file_sessions_init(void)
{
    for (int i = 0; i < MAX_FILE_SESSIONS; i++)
        g_file_sessions[i].active = false;
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++)
        g_upload_sessions[i].active = false;
}
// 设置上传会话
void server_upload_session_set(int uploader_fd, int file_fd, const char* filename)
{
    pthread_mutex_lock(&g_upload_session_mtx);
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++)
    {
        if (!g_upload_sessions[i].active)
        {
            g_upload_sessions[i].uploader_fd = uploader_fd;
            g_upload_sessions[i].file_fd = file_fd;
            strncpy(g_upload_sessions[i].filename, filename, sizeof(g_upload_sessions[i].filename) - 1);
            g_upload_sessions[i].received = 0;
            g_upload_sessions[i].active = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_upload_session_mtx);
}
// 查找上传会话
static int server_upload_session_find_fd(int uploader_fd)
{
    pthread_mutex_lock(&g_upload_session_mtx);
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++)
    {
        if (g_upload_sessions[i].active && g_upload_sessions[i].uploader_fd == uploader_fd)
        {
            int file_fd = g_upload_sessions[i].file_fd;
            pthread_mutex_unlock(&g_upload_session_mtx);
            return file_fd;
        }
    }
    pthread_mutex_unlock(&g_upload_session_mtx);
    return -1;
}
// 清理上传会话
static void server_upload_session_remove(int uploader_fd)
{
    pthread_mutex_lock(&g_upload_session_mtx);
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++)
    {
        if (g_upload_sessions[i].active && g_upload_sessions[i].uploader_fd == uploader_fd)
        {
            if (g_upload_sessions[i].file_fd >= 0)
            {
                close(g_upload_sessions[i].file_fd);
            }
            g_upload_sessions[i].active = false;
            g_upload_sessions[i].file_fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_upload_session_mtx);
}
// 设置 session
void server_file_session_set(int sender_fd, ClientConn* target)
{
    pthread_mutex_lock(&g_file_session_mtx);
    for (int i = 0; i < MAX_FILE_SESSIONS; i++)
    {
        if (!g_file_sessions[i].active)
        {
            g_file_sessions[i].sender_fd = sender_fd;
            g_file_sessions[i].target = target;
            g_file_sessions[i].active = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_file_session_mtx);
}
// 查找 session
static ClientConn* server_file_session_find(int sender_fd)
{
    pthread_mutex_lock(&g_file_session_mtx);
    for (int i = 0; i < MAX_FILE_SESSIONS; i++) {
        if (g_file_sessions[i].active && g_file_sessions[i].sender_fd == sender_fd) {
            ClientConn* target = g_file_sessions[i].target;
            pthread_mutex_unlock(&g_file_session_mtx);
            return target;
        }
    }
    pthread_mutex_unlock(&g_file_session_mtx);
    return NULL;
}
// 清理 session
static void server_file_session_remove(int sender_fd)
{
    pthread_mutex_lock(&g_file_session_mtx);
    for (int i = 0; i < MAX_FILE_SESSIONS; i++)
    {
        if (g_file_sessions[i].active && g_file_sessions[i].sender_fd == sender_fd)
        {
            g_file_sessions[i].active = false;
            g_file_sessions[i].target = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_file_session_mtx);
}
// 清理某个目标的所有 session（目标断开时调用）
static void server_file_session_remove_by_target(ClientConn* target)
{
    pthread_mutex_lock(&g_file_session_mtx);
    for (int i = 0; i < MAX_FILE_SESSIONS; i++)
    {
        if (g_file_sessions[i].active && g_file_sessions[i].target == target)
        {
            g_file_sessions[i].active = false;
            g_file_sessions[i].target = NULL;
        }
    }
    pthread_mutex_unlock(&g_file_session_mtx);
}
/************************ 客户端管理器内部实现 ************************/
// 监听socket（server_shutdown 需要关闭它来让 accept() 退出）
static int g_listen_fd = -1;
// 管理器全局实例（链表头 + 互斥锁）
static struct
{
    ClientConn* head;    // 链表头指针
    int count;           // 当前在线客户端数
    pthread_mutex_t mtx; // 管理器互斥锁
} g_mgr = {NULL, 0, PTHREAD_MUTEX_INITIALIZER};
void client_mgr_init(void)
{
    g_mgr.head = NULL;
    g_mgr.count = 0;
    pthread_mutex_init(&g_mgr.mtx, NULL);
}
int client_mgr_add(ClientConn* conn)
{
    if (!conn)
        return -1;
    pthread_mutex_lock(&g_mgr.mtx);
    if (g_mgr.count >= MAX_CLIENTS)
    {
        pthread_mutex_unlock(&g_mgr.mtx);
        return -1;
    }
    conn->next = g_mgr.head;
    g_mgr.head = conn;
    g_mgr.count++;
    pthread_mutex_unlock(&g_mgr.mtx);
    return 0;
}
void client_mgr_remove(ClientConn* conn)
{
    if (!conn)
        return;
    pthread_mutex_lock(&g_mgr.mtx);
    // 链表移除
    if (g_mgr.head == conn)
    {
        g_mgr.head = conn->next;
    }
    else
    {
        ClientConn* prev = g_mgr.head;
        while (prev && prev->next != conn)
            prev = prev->next;
        if (prev)
            prev->next = conn->next;
    }
    conn->next = NULL;
    g_mgr.count--;
    pthread_mutex_unlock(&g_mgr.mtx);
}
int client_mgr_set_id(ClientConn* conn, const char* id)
{
    if (!conn || !id || *id == '\0')
        return -1;
    pthread_mutex_lock(&g_mgr.mtx);
    // 检查ID是否已被其他客户端占用
    ClientConn* cur = g_mgr.head;
    while (cur)
    {
        if (cur != conn && strcmp(cur->id, id) == 0)
        {
            pthread_mutex_unlock(&g_mgr.mtx);
            return -1; // ID已被占用
        }
        cur = cur->next;
    }
    // 设置新ID
    strncpy(conn->id, id, CLIENT_ID_LEN - 1);
    conn->id[CLIENT_ID_LEN - 1] = '\0';
    pthread_mutex_unlock(&g_mgr.mtx);
    return 0;
}
ClientConn* client_mgr_find_by_id(const char* id)
{
    if (!id || *id == '\0')
        return NULL;
    pthread_mutex_lock(&g_mgr.mtx);
    ClientConn* cur = g_mgr.head;
    while (cur) {
        if (strcmp(cur->id, id) == 0) {
            pthread_mutex_unlock(&g_mgr.mtx);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&g_mgr.mtx);
    return NULL;
}
int client_mgr_broadcast(Packet* pkt, int exclude_fd)
{
    if (!pkt)
        return 0;
    pthread_mutex_lock(&g_mgr.mtx);
    int sent = 0;
    ClientConn* cur = g_mgr.head;
    while (cur)
    {
        if (cur->fd != exclude_fd && cur->running)
        {
            if (client_send_enqueue(cur, pkt) == 0)
                sent++;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&g_mgr.mtx);
    return sent;
}
int client_mgr_send_to_id(const char* id, Packet* pkt)
{
    if (!id || !pkt)
        return -1;
    ClientConn* target = client_mgr_find_by_id(id);
    int ret = -1;
    if (target && target->running)
    {
        ret = client_send_enqueue(target, pkt);
    }
    return ret;
}
int client_mgr_count(void)
{
    pthread_mutex_lock(&g_mgr.mtx);
    int n = g_mgr.count;
    pthread_mutex_unlock(&g_mgr.mtx);
    return n;
}
void client_mgr_list(void)
{
    pthread_mutex_lock(&g_mgr.mtx);
    printf("=== Online Clients (%d) ===\n", g_mgr.count);
    ClientConn* cur = g_mgr.head;
    int idx = 1;
    while (cur)
    {
        printf("  [%d] fd=%d id=%s\n", idx++, cur->fd, cur->id);
        cur = cur->next;
    }
    printf("===========================\n");
    pthread_mutex_unlock(&g_mgr.mtx);
}

int client_mgr_get_info_list(ClientInfo* out_buf, int max_count)
{
    if (!out_buf || max_count <= 0) return 0;
    pthread_mutex_lock(&g_mgr.mtx);
    int n = 0;
    ClientConn* cur = g_mgr.head;
    while (cur && n < max_count)
    {
        strncpy(out_buf[n].id, cur->id, sizeof(out_buf[n].id) - 1);
        out_buf[n].id[sizeof(out_buf[n].id) - 1] = '\0';
        strncpy(out_buf[n].ip, cur->client_ip, sizeof(out_buf[n].ip) - 1);
        out_buf[n].ip[sizeof(out_buf[n].ip) - 1] = '\0';
        out_buf[n].port = cur->client_port;
        n++;
        cur = cur->next;
    }
    pthread_mutex_unlock(&g_mgr.mtx);
    return n;
}

void client_mgr_list_to(ClientConn* conn)
{
    if (!conn)
        return;
    char buf[2048] = {0};
    int offset = 0;
    pthread_mutex_lock(&g_mgr.mtx);
    offset += snprintf(buf + offset, sizeof(buf) - offset, "=== Online Clients (%d) ===\n", g_mgr.count);
    ClientConn* cur = g_mgr.head;
    int idx = 1;
    while (cur && (size_t) offset < sizeof(buf) - 80)
    {
        offset += snprintf(buf + offset, sizeof(buf) - offset, "[%d] fd=%d id=%s\n", idx++, cur->fd, cur->id);
        cur = cur->next;
    }
    offset += snprintf(buf + offset, sizeof(buf) - offset, "===========================\n");
    pthread_mutex_unlock(&g_mgr.mtx);
    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) buf, strlen(buf));
    client_send_enqueue(conn, &pkt);
    packet_free_body(&pkt);
}
/************************ 断开连接与关闭服务器 ************************/
void client_mgr_disconnect(const char* id)
{
    ClientConn* conn = NULL;
    // 查找并移除
    pthread_mutex_lock(&g_mgr.mtx);
    ClientConn* cur = g_mgr.head;
    ClientConn* prev = NULL;
    while (cur)
    {
        if (strcmp(cur->id, id) == 0)
        {
            conn = cur;
            if (prev)
                prev->next = cur->next;
            else
                g_mgr.head = cur->next;
            conn->next = NULL;
            g_mgr.count--;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&g_mgr.mtx);
    if (!conn)
    {
        printf("[disconnect] client '%s' not found\n", id);
        return;
    }
    printf("[disconnect] disconnecting client '%s' (fd=%d)\n", conn->id, conn->fd);
    // 通知线程退出 + 关闭 socket
    conn->running = false;
    if (conn->fd > 0)
    {
        shutdown(conn->fd, SHUT_RDWR);
        close(conn->fd);
        conn->fd = -1;
    }
    // 等待线程退出（recv 线程退出时会自动调用 client_conn_free 释放连接）
    pthread_join(conn->recv_tid, NULL);
    // 注意：不再释放资源，recv 线程已经在退出路径中调用了 client_conn_free
    printf("[disconnect] client '%s' disconnected\n", id);
}

void server_shutdown(void)
{
    // 防重入判断
    pthread_mutex_lock(&g_mgr.mtx);
    if (g_server_shutting_down)
    {
        pthread_mutex_unlock(&g_mgr.mtx);
        return;
    }
    g_server_shutting_down = true;
    pthread_mutex_unlock(&g_mgr.mtx);

    printf("[server] shutting down...\n");

    // 1. 先关闭监听fd，唤醒阻塞accept
    if (g_listen_fd > 0)
    {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    // 2. 清理所有上传会话，关闭本地文件fd，释放文件资源
    pthread_mutex_lock(&g_upload_session_mtx);
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++)
    {
        if (g_upload_sessions[i].active)
        {
            if (g_upload_sessions[i].file_fd >= 0)
            {
                close(g_upload_sessions[i].file_fd);
            }
            g_upload_sessions[i].active = false;
            g_upload_sessions[i].file_fd = -1;
        }
    }
    pthread_mutex_unlock(&g_upload_session_mtx);

    // 3. 清空客户端互传文件会话
    pthread_mutex_lock(&g_file_session_mtx);
    memset(g_file_sessions, 0, sizeof(g_file_sessions));
    pthread_mutex_unlock(&g_file_session_mtx);

    // 4. 逐个遍历连接：关闭 socket、等待线程退出、就地释放资源
    //    不使用一次性数组暂存后再遍历（避免线程回调 client_mgr_remove 造成链表 next 野指针问题）
    pthread_mutex_lock(&g_mgr.mtx);
    ClientConn* cur = g_mgr.head;
    while (cur)
    {
        ClientConn* next = cur->next; // 先保存 next，避免释放后无法访问
        cur->running = false;
        if (cur->fd > 0)
        {
            shutdown(cur->fd, SHUT_RDWR);
            close(cur->fd);
            cur->fd = -1;
        }
        pthread_mutex_unlock(&g_mgr.mtx);

        // 等待该连接的线程退出（此时 g_server_shutting_down 已置位，
        // recv 线程会跳过 client_conn_free，避免与本处重复释放）
        printf("[server] waiting for client '%s'...\n", cur->id);
        pthread_join(cur->recv_tid, NULL);
        pthread_join(cur->send_tid, NULL);

        // 释放连接资源
        for (int j = 0; j < cur->q_capacity; j++)
            packet_free_body(&cur->send_queue[j]);
        free(cur->send_queue);
        pthread_mutex_destroy(&cur->send_mtx);
        pthread_mutex_destroy(&cur->socket_mtx);

        pthread_mutex_lock(&g_mgr.mtx);
        // 从链表中移除（此时线程已退出，安全操作）
        if (g_mgr.head == cur)
        {
            g_mgr.head = cur->next;
        }
        else
        {
            ClientConn* prev = g_mgr.head;
            while (prev && prev->next != cur)
                prev = prev->next;
            if (prev)
                prev->next = cur->next;
        }
        g_mgr.count--;
        free(cur);

        cur = next;
    }
    pthread_mutex_unlock(&g_mgr.mtx);

    // 6. 全部连接释放完毕，销毁所有全局互斥锁（无线程再访问锁）
    pthread_mutex_destroy(&g_upload_session_mtx);
    pthread_mutex_destroy(&g_file_session_mtx);
    pthread_mutex_destroy(&g_mgr.mtx);

    printf("[server] shutdown complete\n");
}

/************************ 连接创建与释放 ************************/
/**
 * @brief 初始化客户端连接上下文，创建发送队列、互斥锁，加入管理器
 */
static ClientConn* client_conn_create(int client_fd)
{
    ClientConn* conn = (ClientConn*) malloc(sizeof(ClientConn));
    if (!conn)
        return NULL;
    memset(conn, 0, sizeof(ClientConn));
    conn->fd = client_fd;
    conn->running = true;
    conn->next = NULL;
    pthread_mutex_init(&conn->send_mtx, NULL);
    pthread_mutex_init(&conn->socket_mtx, NULL);
    // 默认标识 fd:N
    snprintf(conn->id, CLIENT_ID_LEN, "fd%d", client_fd);
    // 分配发送Packet队列
    conn->q_capacity = SEND_QUEUE_CAP;
    conn->send_queue = (Packet*) malloc(sizeof(Packet) * conn->q_capacity);
    if (!conn->send_queue)
    {
        pthread_mutex_destroy(&conn->send_mtx);
        pthread_mutex_destroy(&conn->socket_mtx);
        free(conn);
        return NULL;
    }
    for (int i = 0; i < conn->q_capacity; i++)
    {
        packet_init(&conn->send_queue[i]);
    }
    conn->q_head = conn->q_tail = 0;
    // 加入管理器
    if (client_mgr_add(conn) != 0)
    {
        free(conn->send_queue);
        pthread_mutex_destroy(&conn->send_mtx);
        pthread_mutex_destroy(&conn->socket_mtx);
        free(conn);
        return NULL;
    }
    return conn;
}
/**
 * @brief 释放连接资源，从管理器移除，不等待线程
 */
static void client_conn_free(ClientConn* conn)
{
    if (!conn)
        return;
    // 先从管理器移除
    client_mgr_remove(conn);
    // 关闭socket
    if (conn->fd > 0)
    {
        close(conn->fd);
        conn->fd = -1;
    }
    // 释放队列内所有数据包body
    for (int i = 0; i < conn->q_capacity; i++)
    {
        packet_free_body(&conn->send_queue[i]);
    }
    free(conn->send_queue);
    pthread_mutex_destroy(&conn->send_mtx);
    pthread_mutex_destroy(&conn->socket_mtx);
    free(conn);
}

void client_conn_destroy(ClientConn* conn)
{
    if (!conn)
        return;
    conn->running = false;
    if (conn->fd > 0)
    {
        shutdown(conn->fd, SHUT_RDWR);
        close(conn->fd);
        conn->fd = -1;
    }
    // 等待收发线程退出
    pthread_join(conn->recv_tid, NULL);
    pthread_join(conn->send_tid, NULL);
    client_conn_free(conn);
}

/************************ 文件传输处理函数 ************************/
static void server_handle_file_meta(ClientConn* conn, Packet* recv_pkt)
{
    char* body_str = (char*) recv_pkt->body;
    if (!body_str || recv_pkt->body_len == 0)
        return;
    char meta_buf[512] = {0};
    uint32_t copy_len = recv_pkt->body_len < sizeof(meta_buf) - 1 ? recv_pkt->body_len : sizeof(meta_buf) - 1;
    memcpy(meta_buf, body_str, copy_len);
    meta_buf[copy_len] = '\0';
    char* sep = strchr(meta_buf, '|');
    if (!sep)
        return;
    *sep = '\0';
    const char* target_id = meta_buf;
    const char* rest = sep + 1;
    ClientConn* target = client_mgr_find_by_id(target_id);
    if (!target)
    {
        printf("[%s] file meta target '%s' not found\n", conn->id, target_id);
        return;
    }
    char fwd_meta[512];
    snprintf(fwd_meta, sizeof(fwd_meta), "%s|%s", conn->id, rest);
    Packet fwd_pkt;
    packet_init(&fwd_pkt);
    packet_set_body(&fwd_pkt, PKT_FILE_META, 0, (uint8_t*) fwd_meta, strlen(fwd_meta));
    client_send_enqueue(target, &fwd_pkt);
    packet_free_body(&fwd_pkt);
    server_file_session_set(conn->fd, target);
    // 状态机：发送方进入文件转发状态
    conn->state = STATE_RELAYING_FILE;
    printf("[%s] file transfer session: %s -> %s\n", conn->id, conn->id, target_id);
}
static void server_handle_file_data(ClientConn* conn, Packet* recv_pkt)
{
    // 先检查是否是上传到服务端的情况
    int upload_fd = server_upload_session_find_fd(conn->fd);
    if (upload_fd >= 0)
    {
        // 上传到服务端，写入文件
        if (recv_pkt->body && recv_pkt->body_len > 0)
        {
            ssize_t written = write(upload_fd, recv_pkt->body, recv_pkt->body_len);
            if (written < 0 || (size_t) written != recv_pkt->body_len)
            {
                printf("[%s] upload write error\n", conn->id);
            }
        }
        return;
    }
    // 否则是客户端之间传输，转发给目标
    ClientConn* target = server_file_session_find(conn->fd);
    if (!target)
    {
        printf("[%s] no file session for file data\n", conn->id);
        return;
    }
    // 直接通过 socket 转发，绕过发送队列，避免队列满丢包
    pthread_mutex_lock(&target->socket_mtx);
    pkt_send_full(target->fd, recv_pkt);
    pthread_mutex_unlock(&target->socket_mtx);
}
static void server_handle_file_end(ClientConn* conn, Packet* recv_pkt)
{
    // 先检查是否是上传到服务端的情况
    int upload_fd = server_upload_session_find_fd(conn->fd);
    if (upload_fd >= 0)
    {
        // 上传到服务端，关闭文件并清理会话
        server_upload_session_remove(conn->fd);
        char done_msg[128];
        snprintf(done_msg, sizeof(done_msg), "upload complete");
        Packet notify_pkt;
        packet_init(&notify_pkt);
        packet_set_body(&notify_pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) done_msg, strlen(done_msg));
        client_send_enqueue(conn, &notify_pkt);
        packet_free_body(&notify_pkt);
        // 状态机：上传完成，回到空闲
        conn->state = STATE_IDLE;
        printf("[%s] upload complete\n", conn->id);
        return;
    }
    // 否则是客户端之间传输，转发结束包给目标
    ClientConn* target = server_file_session_find(conn->fd);
    if (!target)
    {
        printf("[%s] no file session for file end\n", conn->id);
        return;
    }
    packet_set_body(recv_pkt, PKT_FILE_END, 0, NULL, 0);
    // 直接通过 socket 转发结束包
    pthread_mutex_lock(&target->socket_mtx);
    pkt_send_full(target->fd, recv_pkt);
    pthread_mutex_unlock(&target->socket_mtx);
    server_file_session_remove(conn->fd);
    char done_msg[128];
    snprintf(done_msg, sizeof(done_msg), "file transfer complete");
    Packet notify_pkt;
    packet_init(&notify_pkt);
    packet_set_body(&notify_pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) done_msg, strlen(done_msg));
    client_send_enqueue(conn, &notify_pkt);
    packet_free_body(&notify_pkt);
    // 状态机：文件转发完成，回到空闲
    conn->state = STATE_IDLE;
}
/************************ 收发线程 ************************/
static void* client_recv_thread(void* arg)
{
    ClientConn* conn = (ClientConn*) arg;
    int fd = conn->fd;
    Packet recv_pkt;
    printf("[%d][%s] recv thread start\n", fd, conn->id);
    while (conn->running)
    {
        // 接收完整数据包
        int ret = pkt_recv_full(fd, &recv_pkt);
        if (ret != 0)
        {
            printf("[%d][%s] recv packet fail, disconnect\n", fd, conn->id);
            conn->running = false; // 通知发送线程退出
            break;
        }
        // 校验包头
        int check_code = pkt_header_check(&recv_pkt.hdr);
        if (check_code != RESP_OK)
        {
            printf("[%d][%s] header verify failed, code=%d\n", fd, conn->id, check_code);
            packet_free_body(&recv_pkt);
            continue;
        }
        // 解析数据包类型
        uint16_t pkt_type = be16toh(recv_pkt.hdr.pkt_type);
        uint32_t blen = recv_pkt.body_len;
        printf("[%d][%s] recv type:%d len:%u\n", fd, conn->id, pkt_type, blen);
        // 按数据包类型分发业务处理
        switch (pkt_type)
        {
            case PKT_MSG_TEXT:
                cmd_server_dispatch(conn, &recv_pkt);
                break;
            case PKT_HEARTBEAT:
                printf("[%d][%s] heartbeat\n", fd, conn->id);
                break;
            case PKT_FILE_META:
                server_handle_file_meta(conn, &recv_pkt);
                break;
            case PKT_FILE_DATA:
                server_handle_file_data(conn, &recv_pkt);
                break;
            case PKT_FILE_END:
                server_handle_file_end(conn, &recv_pkt);
                break;
            default:
                printf("[%d][%s] unknown pkt type:%d\n", fd, conn->id, pkt_type);
                break;
        }
        // 释放本次接收包体
        packet_free_body(&recv_pkt);
    }
    // 清理文件传输 session
    server_file_session_remove(fd);
    server_file_session_remove_by_target(conn);
    printf("[%d][%s] recv thread exit\n", fd, conn->id);

    // 服务器正在关闭时，跳过下线广播，避免刷屏
    if (!g_server_shutting_down)
    {
        // 广播下线通知
        char offline_msg[128];
        snprintf(offline_msg, sizeof(offline_msg), "[system] %s offline", conn->id);
        Packet notify_pkt;
        packet_init(&notify_pkt);
        packet_set_body(&notify_pkt, PKT_MSG_TEXT, CMD_MSG, (uint8_t*) offline_msg, strlen(offline_msg));
        client_mgr_broadcast(&notify_pkt, conn->fd);
        packet_free_body(&notify_pkt);
    }

    // 等待发送线程退出
    pthread_join(conn->send_tid, NULL);
    // 释放连接资源（正常断连时由 recv 线程负责释放）
    // 注意：server_shutdown() 场景下必须跳过，否则与 shutdown 清理形成 double free
    if (!g_server_shutting_down)
    {
        client_conn_free(conn);
    }
    pthread_exit(NULL);
}
static void *client_send_thread(void* arg)
{
    ClientConn* conn = (ClientConn*) arg;
    int fd = conn->fd;
    Packet* pkt = NULL;
    printf("[%d][%s] send thread start\n", fd, conn->id);
    while (conn->running)
    {
        pkt = client_send_dequeue(conn);
        if (!pkt)
        {
            usleep(10000); // 队列为空短暂休眠
            continue;
        }
        pthread_mutex_lock(&conn->socket_mtx);
        int ret = pkt_send_full(fd, pkt);
        pthread_mutex_unlock(&conn->socket_mtx);
        if (ret != 0)
        {
            printf("[%d] send pkt fail\n", fd);
        }
        packet_free_body(pkt);
    }
    printf("[%d][%s] send thread exit\n", fd, conn->id);
    pthread_exit(NULL);
}
/************************ accept处理与server入口 ************************/
/**
 * @brief 新建客户端连接，创建收发双线程
 */
static void handle_accept(int client_fd, struct sockaddr_in* client_addr)
{
    ClientConn* conn = client_conn_create(client_fd);
    if (!conn)
    {
        close(client_fd);
        printf("malloc client conn fail or max clients reached\n");
        return;
    }
    // 记录客户端IP和端口
    if (client_addr)
    {
        inet_ntop(AF_INET, &client_addr->sin_addr, conn->client_ip, sizeof(conn->client_ip));
        conn->client_port = ntohs(client_addr->sin_port);
    }
    // 创建接收线程
    if (pthread_create(&conn->recv_tid, NULL, client_recv_thread, conn) != 0)
    {
        client_conn_free(conn);
        printf("create recv thread fail\n");
        return;
    }
    // 创建发送线程
    if (pthread_create(&conn->send_tid, NULL, client_send_thread, conn) != 0)
    {
        conn->running = false;
        pthread_join(conn->recv_tid, NULL);
        client_conn_free(conn);
        printf("create send thread fail\n");
        return;
    }
    printf("new client fd:%d id:%s, online count:%d\n", client_fd, conn->id, client_mgr_count());
    // 广播上线通知
    char online_msg[128];
    snprintf(online_msg, sizeof(online_msg), "[system] %s online", conn->id);
    Packet notify_pkt;
    packet_init(&notify_pkt);
    packet_set_body(&notify_pkt, PKT_MSG_TEXT, CMD_MSG, (uint8_t*) online_msg, strlen(online_msg));
    client_mgr_broadcast(&notify_pkt, conn->fd);
    packet_free_body(&notify_pkt);
    // 通知客户端自己的ID
    char id_msg[128];
    snprintf(id_msg, sizeof(id_msg), "id set ok: %s", conn->id);
    Packet id_pkt;
    packet_init(&id_pkt);
    packet_set_body(&id_pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) id_msg, strlen(id_msg));
    client_send_enqueue(conn, &id_pkt);
    packet_free_body(&id_pkt);
}
void server_start(void)
{
    // 从配置读取端口
    int port = SERVER_LISTEN_PORT;
    AppConfig* cfg = config_get_global();
    if (cfg && cfg->server_port >= SERVER_PORT_MIN && cfg->server_port <= SERVER_PORT_MAX)
        port = cfg->server_port;

    // 初始化客户端管理器
    client_mgr_init();
    // 初始化文件传输会话
    server_file_sessions_init();
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0)
    {
        perror("socket create fail");
        return;
    }
    // 端口复用
    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_listen_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind fail");
        close(g_listen_fd);
        return;
    }
    if (listen(g_listen_fd, SERVER_BACKLOG) < 0)
    {
        perror("listen fail");
        close(g_listen_fd);
        return;
    }
    printf("server listen port %d, max clients %d\n", port, MAX_CLIENTS);
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr*) &client_addr, &addr_len);
        if (client_fd < 0)
        {
            if (g_listen_fd < 0) {
                printf("server stopped\n");
                break;
            }
            perror("accept fail");
            continue;
        }
        if (client_mgr_count() >= MAX_CLIENTS)
        {
            printf("max clients reached, reject fd:%d\n", client_fd);
            close(client_fd);
            continue;
        }
        handle_accept(client_fd, &client_addr);
    }
}