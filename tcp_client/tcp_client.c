#include "tcp_client.h"
#include "tcp_cmd_parser.h"
#include "tcp_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

// 服务端连接配置
char *server_ip = "127.0.0.1";
int server_port = 8888;

// 文件发送上下文
static struct {
    char target_id[CLIENT_ID_LEN];
    char filepath[256];
    uint64_t file_size;
    bool waiting_accept;
} g_file_send_ctx;

// 文件接收上下文
static struct {
    bool active;
    int fd;                     // 接收文件描述符
    char filename[256];
    char save_path[256];
    uint64_t total_size;
    uint64_t received;
} g_file_recv_ctx;

// 文件上传上下文（上传到服务端）
static struct {
    bool active;
    char filepath[512];
    char filename[256];
    uint64_t file_size;
} g_file_upload_ctx;

// 当前文件发送者ID（用于 file_accept file_reject 命令）
char g_file_sender_id[64] = {0};

// 待接收文件名（从 PKT_FILE_META 中获取，用于自动命名）
static char g_pending_file_name[256] = {0};

// 本地客户端ID，用于提示符（避免到处传 conn）
static char g_client_id[CLIENT_ID_LEN] = "";

static void print_prompt(void)
{
    printf("%s: ", g_client_id[0] ? g_client_id : "?");
    fflush(stdout);
}

/**
 * @brief 创建TCP socket并连接服务端
 */
static int client_connect_server(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket create fail");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton ip error");
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect server failed");
        close(fd);
        return -1;
    }
    printf("connect server %s:%d success\n", server_ip, server_port);
    return fd;
}

ClientConn* client_create(void)
{
    int fd = client_connect_server();
    if (fd < 0)
        return NULL;

    ClientConn* conn = malloc(sizeof(ClientConn));
    if (!conn) {
        close(fd);
        return NULL;
    }
    memset(conn, 0, sizeof(ClientConn));
    conn->fd = fd;
    conn->state = STATE_IDLE;
    conn->running = true;
    conn->q_capacity = SEND_QUEUE_CAP;
    snprintf(conn->id, CLIENT_ID_LEN, "fd%d", fd);
    // g_client_id 初始为空，等服务端发 welcome 包后更新

    // 初始化互斥锁和条件变量
    pthread_mutex_init(&conn->send_mtx, NULL);
    pthread_mutex_init(&conn->socket_mtx, NULL);
    // 分配发送队列
    conn->send_queue = malloc(sizeof(Packet) * conn->q_capacity);
    if (!conn->send_queue) {
        pthread_mutex_destroy(&conn->send_mtx);
        pthread_mutex_destroy(&conn->socket_mtx);
        free(conn);
        close(fd);
        return NULL;
    }
    for (int i = 0; i < conn->q_capacity; i++) {
        packet_init(&conn->send_queue[i]);
    }
    conn->q_head = conn->q_tail = 0;

    // 创建recv线程
    if (pthread_create(&conn->recv_tid, NULL, client_recv_thread, conn) != 0) {
        client_destroy(conn);
        return NULL;
    }
    // 创建send线程
    if (pthread_create(&conn->send_tid, NULL, client_send_thread, conn) != 0) {
        conn->running = false;
        pthread_join(conn->recv_tid, NULL);
        // 清理资源
        for (int i = 0; i < conn->q_capacity; i++)
            packet_free_body(&conn->send_queue[i]);
        free(conn->send_queue);
        pthread_mutex_destroy(&conn->send_mtx);
        pthread_mutex_destroy(&conn->socket_mtx);
        close(fd);
        free(conn);
        return NULL;
    }
    return conn;
}

void client_destroy(ClientConn* conn)
{
    if (!conn) return;
    conn->running = false;

    // shutdown(SHUT_RD) 让 recv_thread 的 recv 立即返回错误
    // 之后 close(fd)，再 join 线程
    if (conn->fd > 0) {
        shutdown(conn->fd, SHUT_RD);
        close(conn->fd);
    }

    // 等待收发线程退出
    pthread_join(conn->recv_tid, NULL);
    pthread_join(conn->send_tid, NULL);

    // 释放队列所有包体内存
    for (int i = 0; i < conn->q_capacity; i++) {
        packet_free_body(&conn->send_queue[i]);
    }
    free(conn->send_queue);
    pthread_mutex_destroy(&conn->send_mtx);
    pthread_mutex_destroy(&conn->socket_mtx);
    free(conn);
    printf("client resource released\n");
}

/************************************* 客户端命令处理函数 *************************************/

/**
 * @brief 处理 /file @target path 命令：保存发送上下文
 * @return 0 继续发送，-1 失败（内部已释放 pkt）
 */
static int exec_cmd_file_put(ClientConn* conn, Packet* pkt)
{
    char* body_str = (char*)pkt->body;
    if (!body_str || pkt->body_len == 0 || body_str[0] != '@')
        return -1;

    // 拷贝到本地缓冲区解析，避免修改原始包体
    char local_buf[1024];
    uint32_t copy_len = pkt->body_len < sizeof(local_buf) - 1 ? pkt->body_len : sizeof(local_buf) - 1;
    memcpy(local_buf, body_str, copy_len);
    local_buf[copy_len] = '\0';

    char* sep1 = strchr(local_buf + 1, '|');
    if (!sep1) return -1;
    *sep1 = '\0';
    strncpy(g_file_send_ctx.target_id, local_buf + 1, sizeof(g_file_send_ctx.target_id) - 1);

    char* sep2 = strchr(sep1 + 1, '|');
    if (sep2) {
        *sep2 = '\0';
        g_file_send_ctx.file_size = strtoull(sep2 + 1, NULL, 10);
    }
    strncpy(g_file_send_ctx.filepath, sep1 + 1, sizeof(g_file_send_ctx.filepath) - 1);

    // ========== 拦截检查：如果目标是自己，直接拒绝 ==========
    if (strcmp(g_file_send_ctx.target_id, conn->id) == 0)
    {
        printf("[File] Cannot send file to yourself: %s\n", g_file_send_ctx.target_id);
        return -1;
    }

    g_file_send_ctx.waiting_accept = true;
    conn->state = STATE_SENDING_FILE;
    printf("file transfer request sent, waiting for target to accept...\n");
    return 0;
}

/**
 * @brief 处理 file_accept [path] 命令：打开文件准备写入
 * @return 0 继续发送，-1 失败（内部已释放 pkt 并重置状态）
 */
static int exec_cmd_file_accept(ClientConn* conn, Packet* pkt)
{
    char* body_str = (char*)pkt->body;
    if (!body_str || pkt->body_len == 0)
    {
        conn->state = STATE_IDLE;
        g_file_sender_id[0] = '\0';
        return -1;
    }

    char* sep = strchr(body_str, '|');
    if (!sep)
    {
        conn->state = STATE_IDLE;
        g_file_sender_id[0] = '\0';
        return -1;
    }

    const char* save_path = sep + 1;
    if (*save_path == '\0') save_path = "./";

    // 如果路径是目录，追加文件名
    char final_path[512];
    struct stat path_stat;
    if (stat(save_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode))
    {
        size_t len = strlen(save_path);
        const char* sep_char = (len > 0 && save_path[len - 1] == '/') ? "" : "/";
        snprintf(final_path, sizeof(final_path), "%s%s%s", save_path, sep_char, g_pending_file_name);
    }
    else
    {
        strncpy(final_path, save_path, sizeof(final_path) - 1);
        final_path[sizeof(final_path) - 1] = '\0';
    }

    int recv_fd = open(final_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (recv_fd < 0)
    {
        printf("cannot open %s for writing\n", final_path);
        conn->state = STATE_IDLE;
        g_file_sender_id[0] = '\0';
        return -1;
    }

    g_file_recv_ctx.fd = recv_fd;
    g_file_recv_ctx.active = true;
    strncpy(g_file_recv_ctx.save_path, final_path, sizeof(g_file_recv_ctx.save_path) - 1);
    g_file_recv_ctx.received = 0;
    conn->state = STATE_RECEIVING_FILE;
    printf("receiving file, saving to: %s\n", final_path);

    // 清除 sender_id，防止重复 accept
    g_file_sender_id[0] = '\0';
    return 0;
}

/**
 * @brief 处理 file_reject 命令：清除 pending 状态
 */
static void exec_cmd_file_reject(ClientConn* conn)
{
    g_file_sender_id[0] = '\0';
    conn->state = STATE_IDLE;
}

/**
 * @brief 处理 /upload filepath 命令：保存上传上下文
 * @return 0 继续发送，-1 失败
 */
static int exec_cmd_file_upload(ClientConn* conn, Packet* pkt)
{
    char* body_str = (char*)pkt->body;
    if (!body_str || pkt->body_len == 0)
        return -1;

    char local_buf[1024];
    uint32_t copy_len = pkt->body_len < sizeof(local_buf) - 1 ? pkt->body_len : sizeof(local_buf) - 1;
    memcpy(local_buf, body_str, copy_len);
    local_buf[copy_len] = '\0';

    // body 格式: filename|filesize|filepath|server_subdir
    char* sep1 = strchr(local_buf, '|');
    if (!sep1) return -1;
    *sep1 = '\0';
    strncpy(g_file_upload_ctx.filename, local_buf, sizeof(g_file_upload_ctx.filename) - 1);

    char* sep2 = strchr(sep1 + 1, '|');
    if (!sep2) return -1;
    *sep2 = '\0';
    g_file_upload_ctx.file_size = strtoull(sep1 + 1, NULL, 10);

    // 第三个字段: filepath
    char* sep3 = strchr(sep2 + 1, '|');
    if (sep3)
    {
        *sep3 = '\0';
        strncpy(g_file_upload_ctx.filepath, sep2 + 1, sizeof(g_file_upload_ctx.filepath) - 1);
        // 第四个字段: server_subdir（可选，客户端仅用于显示）
        const char* subdir = sep3 + 1;
        g_file_upload_ctx.active = true;
        conn->state = STATE_UPLOADING;
        if (*subdir != '\0')
            printf("uploading %s (%llu bytes) to server [%s]...\n",
                   g_file_upload_ctx.filename, (unsigned long long)g_file_upload_ctx.file_size, subdir);
        else
            printf("uploading %s (%llu bytes) to server...\n",
                   g_file_upload_ctx.filename, (unsigned long long)g_file_upload_ctx.file_size);
    }
    else
    {
        // 兼容旧格式: filename|filesize|filepath (无 server_subdir)
        strncpy(g_file_upload_ctx.filepath, sep2 + 1, sizeof(g_file_upload_ctx.filepath) - 1);
        g_file_upload_ctx.active = true;
        conn->state = STATE_UPLOADING;
        printf("uploading %s (%llu bytes) to server...\n",
               g_file_upload_ctx.filename, (unsigned long long)g_file_upload_ctx.file_size);
    }
    return 0;
}

/**
 * @brief 处理 /download filename [save_path] 命令：打开文件准备写入
 * @return 0 继续发送，-1 失败
 */
static int exec_cmd_file_download(ClientConn* conn, Packet* pkt)
{
    char* body_str = (char*)pkt->body;
    if (!body_str || pkt->body_len == 0)
        return -1;

    char local_buf[512];
    uint32_t copy_len = pkt->body_len < sizeof(local_buf) - 1 ? pkt->body_len : sizeof(local_buf) - 1;
    memcpy(local_buf, body_str, copy_len);
    local_buf[copy_len] = '\0';

    char* sep = strchr(local_buf, '|');
    if (!sep) return -1;
    *sep = '\0';
    const char* filename = local_buf;
    const char* save_path = sep + 1;
    if (*save_path == '\0') save_path = "./";

    // 从 filename 中提取纯文件名（去掉服务端目录前缀，如 photo/snap.bmp → snap.bmp）
    const char* pure_name = strrchr(filename, '/');
    pure_name = pure_name ? pure_name + 1 : filename;

    // 如果保存路径是目录，追加纯文件名
    char final_path[512];
    struct stat path_stat;
    if (stat(save_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode))
    {
        size_t len = strlen(save_path);
        const char* sep_char = (len > 0 && save_path[len - 1] == '/') ? "" : "/";
        snprintf(final_path, sizeof(final_path), "%s%s%s", save_path, sep_char, pure_name);
    }
    else
    {
        strncpy(final_path, save_path, sizeof(final_path) - 1);
        final_path[sizeof(final_path) - 1] = '\0';
    }

    // 保存接收上下文，文件创建延后到收到 META 包时进行
    // （避免父目录不存在导致请求根本发不出去）
    g_file_recv_ctx.fd = -1;
    g_file_recv_ctx.active = false;
    strncpy(g_file_recv_ctx.save_path, final_path, sizeof(g_file_recv_ctx.save_path) - 1);
    strncpy(g_file_recv_ctx.filename, filename, sizeof(g_file_recv_ctx.filename) - 1);
    g_file_recv_ctx.received = 0;
    g_file_recv_ctx.total_size = 0;
    conn->state = STATE_DOWNLOADING;
    printf("download request sent: %s (saving to: %s)\n", filename, final_path);
    return 0;
}


int client_input_cmd(ClientConn* conn, const char* cmd)
{
    Packet pkt;
    int ret = packet_from_cmd(cmd, &pkt);
    if (ret != 0)
    {
        printf("parse cmd fail ret=%d\n", ret);
        return ret;
    }

    // ========== 状态机校验：文件类命令仅在 IDLE 状态下允许 ==========
    enum CMD_TYPE cmd_type = pkt.hdr.pkt_sub_type;
    if (cmd_type == CMD_FILE_PUT || cmd_type == CMD_FILE_UPLOAD || cmd_type == CMD_FILE_DOWNLOAD)
    {
        if (conn->state != STATE_IDLE)
        {
            printf("busy, current state: %d\n", conn->state);
            packet_free_body(&pkt);
            return -1;
        }
    }
    // file_accept 和 file_reject 仅在等待确认状态下允许
    if (cmd_type == CMD_FILE_ACCEPT || cmd_type == CMD_FILE_REJECT)
    {
        if (conn->state != STATE_WAITING_FILE_CONFIRM)
        {
            printf("no pending file transfer request\n");
            packet_free_body(&pkt);
            return -1;
        }
    }

    // 分发到各命令处理函数
    switch (cmd_type)
    {
        case CMD_FILE_PUT:
            if (exec_cmd_file_put(conn, &pkt) != 0)
            {
                packet_free_body(&pkt);
                return -1;
            }
            break;
        case CMD_FILE_ACCEPT:
            if (exec_cmd_file_accept(conn, &pkt) != 0)
            {
                packet_free_body(&pkt);
                return -1;
            }
            break;
        case CMD_FILE_REJECT:
            exec_cmd_file_reject(conn);
            break;
        case CMD_FILE_UPLOAD:
            if (exec_cmd_file_upload(conn, &pkt) != 0)
            {
                packet_free_body(&pkt);
                return -1;
            }
            break;
        case CMD_FILE_DOWNLOAD:
            if (exec_cmd_file_download(conn, &pkt) != 0)
            {
                packet_free_body(&pkt);
                return -1;
            }
            break;
        default:
            break;
    }

    ret = client_send_enqueue(conn, &pkt);
    packet_free_body(&pkt);
    return ret;
}

/************************ 推送包处理函数 ************************/

/**
 * @brief 处理 SET_ID 成功推送，更新本地 ID
 */
static void handle_push_set_id(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    if (blen > 11 && strncmp((char*)pkt->body, "id set ok: ", 11) == 0) {
        strncpy(conn->id, (char*)pkt->body + 11, CLIENT_ID_LEN - 1);
        conn->id[CLIENT_ID_LEN - 1] = '\0';
        strncpy(g_client_id, conn->id, CLIENT_ID_LEN - 1);
    }
}

/**
 * @brief 处理文件元信息包，提示用户接收/拒绝
 */
static void handle_push_file_meta(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    if (blen == 0) return;

    char meta[512] = {0};
    uint32_t copy_len = blen < sizeof(meta)-1 ? blen : sizeof(meta)-1;
    memcpy(meta, pkt->body, copy_len);
    meta[copy_len] = '\0';

    char* sep1 = strchr(meta, '|');
    if (!sep1) return;
    *sep1 = '\0';
    const char* sender_id = meta;
    char* sep2 = strchr(sep1 + 1, '|');
    if (!sep2) return;
    *sep2 = '\0';
    const char* fname = sep1 + 1;
    uint64_t fsize = strtoull(sep2 + 1, NULL, 10);

    strncpy(g_file_sender_id, sender_id, sizeof(g_file_sender_id) - 1);
    strncpy(g_pending_file_name, fname, sizeof(g_pending_file_name) - 1);

    // 服务端下载：收到 META 后再创建目录和文件（防止提前 open 失败导致请求没发出去）
    if (strcmp(sender_id, "server") == 0 && conn->state == STATE_DOWNLOADING) {
        // 确保父目录存在
        char* save_path = g_file_recv_ctx.save_path;
        char parent_dir[512];
        strncpy(parent_dir, save_path, sizeof(parent_dir) - 1);
        parent_dir[sizeof(parent_dir) - 1] = '\0';
        char* last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (strlen(parent_dir) > 0) {
                char tmp[512];
                snprintf(tmp, sizeof(tmp), "%s", parent_dir);
                for (char* p = tmp + 1; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        mkdir(tmp, 0755);
                        *p = '/';
                    }
                }
                mkdir(tmp, 0755);
            }
        }

        int fd = open(save_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("\n[File] cannot open %s for writing: %s\n", save_path, strerror(errno));
            conn->state = STATE_IDLE;
            g_file_recv_ctx.active = false;
            g_file_recv_ctx.fd = -1;
            return;
        }
        g_file_recv_ctx.fd = fd;
        g_file_recv_ctx.active = true;
        g_file_recv_ctx.total_size = fsize;
        printf("\n[File] receiving: %s (%llu bytes)\n", fname, (unsigned long long)fsize);
        return;
    }

    // 状态机：收到文件传输请求，进入等待确认状态
    conn->state = STATE_WAITING_FILE_CONFIRM;

    printf("\n[File Transfer] %s wants to send \"%s\" (%llu bytes)\n",
           sender_id, fname, (unsigned long long)fsize);
    printf("Enter \"file_accept [path]\" to accept (path is optional, defaults to ./)\n");
    printf("or \"file_reject\" to reject\n");
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理文件数据包，写入接收文件
 */
static void handle_push_file_data(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    if (!g_file_recv_ctx.active || g_file_recv_ctx.fd < 0) return;

    ssize_t written = write(g_file_recv_ctx.fd, pkt->body, blen);
    if (written < 0 || (size_t)written != blen) {
        printf("\n[File] write error\n");
        close(g_file_recv_ctx.fd);
        g_file_recv_ctx.active = false;
        g_file_recv_ctx.fd = -1;
        // 状态机：写入失败，回到空闲
        conn->state = STATE_IDLE;
        return;
    }
    g_file_recv_ctx.received += blen;
    if (g_file_recv_ctx.total_size > 0) {
        int pct = (int)(g_file_recv_ctx.received * 100 / g_file_recv_ctx.total_size);
        printf("\r[File] receiving: %llu/%llu (%d%%)",
               (unsigned long long)g_file_recv_ctx.received,
               (unsigned long long)g_file_recv_ctx.total_size, pct);
        fflush(stdout);
    }
}

/**
 * @brief 处理文件结束包，关闭接收文件
 */
static void handle_push_file_end(ClientConn* conn)
{
    if (!g_file_recv_ctx.active) return;
    if (g_file_recv_ctx.fd >= 0) {
        close(g_file_recv_ctx.fd);
        g_file_recv_ctx.fd = -1;
    }
    printf("\n[File] received successfully: %s (%llu bytes)\n",
           g_file_recv_ctx.filename,
           (unsigned long long)g_file_recv_ctx.received);
    g_file_recv_ctx.active = false;
    // 状态机：接收完成，回到空闲
    conn->state = STATE_IDLE;
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理服务端错误应答包（PKT_ERR），重置状态机并清理文件上下文
 *        客户端收到此包后，无论当前处于何种文件传输状态，都回到空闲态
 * @param conn 连接上下文
 * @param pkt 错误应答数据包，body 为错误描述，pkt_sub_type 为 ERR_TYPE 枚举值
 */
static void handle_push_error(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    uint8_t  err_type = pkt->hdr.pkt_sub_type;

    const char* err_label = "UNKNOWN";
    switch (err_type) {
        case ERR_FILE_NOT_FOUND:   err_label = "FILE_NOT_FOUND";   break;
        case ERR_FILE_OPEN_FAILED: err_label = "FILE_OPEN_FAILED"; break;
        case ERR_FILE_WRITE_FAILED: err_label = "FILE_WRITE_FAILED"; break;
        case ERR_BUSY:             err_label = "BUSY";             break;
        case ERR_PERMISSION:       err_label = "PERMISSION";       break;
        default: break;
    }

    if (blen > 0) {
        printf("\n[Error:%s] %.*s\n", err_label, blen, pkt->body);
    } else {
        printf("\n[Error:%s] (no details)\n", err_label);
    }

    // 清理文件接收上下文（下载/客户端间传输）
    if (g_file_recv_ctx.active) {
        if (g_file_recv_ctx.fd >= 0) {
            close(g_file_recv_ctx.fd);
            g_file_recv_ctx.fd = -1;
        }
        g_file_recv_ctx.active = false;
    }

    // 清理文件上传上下文
    if (g_file_upload_ctx.active) {
        g_file_upload_ctx.active = false;
    }

    // 清理文件发送上下文（客户端间传输）
    if (g_file_send_ctx.waiting_accept) {
        g_file_send_ctx.waiting_accept = false;
    }

    // 状态机：回到空闲
    conn->state = STATE_IDLE;
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理文件接受应答（发送者收到后开始传文件）
 */
static void handle_push_file_accept(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    if (blen == 0) return;

    char accept_msg[256] = {0};
    uint32_t copy_len = blen < sizeof(accept_msg)-1 ? blen : sizeof(accept_msg)-1;
    memcpy(accept_msg, pkt->body, copy_len);
    accept_msg[copy_len] = '\0';

    char* sep = strchr(accept_msg, '|');
    if (!sep) return;

    printf("\n[File] target accepted, saving to: %s\n", sep + 1);

    int send_fd = open(g_file_send_ctx.filepath, O_RDONLY);
    if (send_fd < 0) {
        printf("[File] cannot open %s for reading\n", g_file_send_ctx.filepath);
        g_file_send_ctx.waiting_accept = false;
        // 状态机：文件打开失败，回到空闲
        conn->state = STATE_IDLE;
        print_prompt();
        fflush(stdout);
        return;
    }

    int fd = conn->fd;

    // 锁住 socket 写，防止发送线程插入其他数据包
    // 文件数据直接通过 socket 发送，绕过发送队列
    pthread_mutex_lock(&conn->socket_mtx);

    uint8_t chunk[FILE_CHUNK_SIZE];
    ssize_t n;
    uint64_t sent = 0;
    while ((n = read(send_fd, chunk, FILE_CHUNK_SIZE)) > 0) {
        Packet data_pkt;
        packet_init(&data_pkt);
        packet_set_body(&data_pkt, PKT_FILE_DATA, 0, chunk, n);

        if (pkt_send_full(fd, &data_pkt) != 0) {
            printf("\n[File] send error\n");
            packet_free_body(&data_pkt);
            break;
        }

        packet_free_body(&data_pkt);
        sent += n;
        printf("\r[File] sending: %llu/%llu", (unsigned long long)sent, (unsigned long long)g_file_send_ctx.file_size);
        fflush(stdout);
    }
    close(send_fd);

    // 发送结束包
    Packet end_pkt;
    packet_init(&end_pkt);
    packet_set_body(&end_pkt, PKT_FILE_END, 0, NULL, 0);
    pkt_send_full(fd, &end_pkt);
    packet_free_body(&end_pkt);

    pthread_mutex_unlock(&conn->socket_mtx);

    printf("\n[File] send complete (%llu bytes)\n", (unsigned long long)sent);
    g_file_send_ctx.waiting_accept = false;
    // 状态机：发送完成，回到空闲
    conn->state = STATE_IDLE;
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理文件拒绝应答
 */
static void handle_push_file_reject(ClientConn* conn, Packet* pkt)
{
    uint32_t blen = pkt->body_len;
    if (blen > 0) {
        printf("\n[File] %.*s\n", blen, pkt->body);
    }
    g_file_send_ctx.waiting_accept = false;
    // 状态机：文件被拒绝，回到空闲
    conn->state = STATE_IDLE;
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理服务端上传确认（开始上传文件到服务端）
 */
static void handle_push_upload_ready(ClientConn* conn, Packet* pkt)
{
    if (!g_file_upload_ctx.active) return;
    
    uint32_t blen = pkt->body_len;
    if (blen == 0) return;
    
    // 检查是否是确认消息
    if (strncmp((char*)pkt->body, "upload ready", 12) != 0) {
        printf("\n[Upload] %.*s\n", blen, pkt->body);
        g_file_upload_ctx.active = false;
        // 状态机：上传被拒绝，回到空闲
        conn->state = STATE_IDLE;
        print_prompt();
        fflush(stdout);
        return;
    }
    
    int send_fd = open(g_file_upload_ctx.filepath, O_RDONLY);
    if (send_fd < 0) {
        printf("\n[Upload] cannot open %s for reading\n", g_file_upload_ctx.filepath);
        g_file_upload_ctx.active = false;
        // 状态机：上传文件打开失败，回到空闲
        conn->state = STATE_IDLE;
        print_prompt();
        fflush(stdout);
        return;
    }
    
    int fd = conn->fd;
    
    // 锁住 socket 写，防止发送线程插入其他数据包
    pthread_mutex_lock(&conn->socket_mtx);
    
    uint8_t chunk[FILE_CHUNK_SIZE];
    ssize_t n;
    uint64_t sent = 0;
    while ((n = read(send_fd, chunk, FILE_CHUNK_SIZE)) > 0) {
        Packet data_pkt;
        packet_init(&data_pkt);
        packet_set_body(&data_pkt, PKT_FILE_DATA, 0, chunk, n);
        
        if (pkt_send_full(fd, &data_pkt) != 0) {
            printf("\n[Upload] send error\n");
            packet_free_body(&data_pkt);
            break;
        }
        
        packet_free_body(&data_pkt);
        sent += n;
        printf("\r[Upload] sending: %llu/%llu", (unsigned long long)sent, (unsigned long long)g_file_upload_ctx.file_size);
        fflush(stdout);
    }
    close(send_fd);
    
    // 发送结束包
    Packet end_pkt;
    packet_init(&end_pkt);
    packet_set_body(&end_pkt, PKT_FILE_END, 0, NULL, 0);
    pkt_send_full(fd, &end_pkt);
    packet_free_body(&end_pkt);
    
    pthread_mutex_unlock(&conn->socket_mtx);
    
    printf("\n[Upload] complete (%llu bytes)\n", (unsigned long long)sent);
    g_file_upload_ctx.active = false;
    // 状态机：上传完成，回到空闲
    conn->state = STATE_IDLE;
    print_prompt();
    fflush(stdout);
}

/**
 * @brief 处理服务端推送消息（广播、私聊、文件等）
 */
static void handle_push_packet(ClientConn* conn, Packet* pkt)
{
    uint16_t pkt_type = be16toh(pkt->hdr.pkt_type);
    uint32_t blen = pkt->body_len;

    handle_push_set_id(conn, pkt);

    switch (pkt_type)
    {
        case PKT_FILE_META:
            handle_push_file_meta(conn, pkt);
            break;

        case PKT_FILE_DATA:
            handle_push_file_data(conn, pkt);
            break;

        case PKT_FILE_END:
            handle_push_file_end(conn);
            break;

        case PKT_ERR:
            handle_push_error(conn, pkt);
            break;

        case PKT_MSG_TEXT:
            if (pkt->hdr.pkt_sub_type == CMD_FILE_ACCEPT) {
                handle_push_file_accept(conn, pkt);
                break;
            }
            if (pkt->hdr.pkt_sub_type == CMD_FILE_REJECT) {
                handle_push_file_reject(conn, pkt);
                break;
            }
            if (pkt->hdr.pkt_sub_type == CMD_FILE_UPLOAD) {
                handle_push_upload_ready(conn, pkt);
                break;
            }
            break;

        default:
            break;
    }

    // 文件数据包、结束包、错误包不重复打印（已在 handler 中处理）
    if (pkt_type != PKT_FILE_DATA && pkt_type != PKT_FILE_END && pkt_type != PKT_ERR) {
        // printf("[Push type=%d sub=%d len=%u]\n", pkt_type, pkt->hdr.pkt_sub_type, blen);
        if (blen > 0)
            printf("%.*s\n", blen, pkt->body);
        else
            printf("\n");
        print_prompt();
        fflush(stdout);
    }
}

/**
 * @brief 接收线程：专职读socket，接收推送包处理打印
 */
void* client_recv_thread(void* arg)
{
    ClientConn* conn = (ClientConn*)arg;
    int fd = conn->fd;

    while (conn->running) 
    {
        Packet pkt;
        packet_init(&pkt);

        int ret = pkt_recv_full(fd, &pkt);
        if (ret != 0) {
            // IO错误或连接断开
            printf("server disconnected\n");
            conn->running = false;
            // 关闭stdin，解除主线程fgets阻塞使其退出
            fclose(stdin);
            break;
        }

        // 所有包统一按推送包处理
        handle_push_packet(conn, &pkt);
        packet_free_body(&pkt);
    }

    printf("client recv thread exit\n");
    pthread_exit(NULL);
}

/**
 * @brief 发送线程：从队列取包发送
 */
void* client_send_thread(void* arg)
{
    ClientConn* conn = (ClientConn*)arg;
    int fd = conn->fd;

    while (conn->running) 
    {
        Packet* pkt = client_send_dequeue(conn);
        if (!pkt) {
            usleep(10000);
            continue;
        }

        // 加socket写锁，防止收发线程并发写同一fd
        pthread_mutex_lock(&conn->socket_mtx);

        // 发送完整数据包（包头+包体）
        if (send_n(fd, &pkt->hdr, HEADER_SIZE) != 0) {
            pthread_mutex_unlock(&conn->socket_mtx);
            printf("server disconnected\n");
            conn->running = false;
            packet_free_body(pkt);
            break;
        }
        if (pkt->body_len > 0 && pkt->body != NULL) {
            if (send_n(fd, pkt->body, pkt->body_len) != 0) {
                pthread_mutex_unlock(&conn->socket_mtx);
                printf("server disconnected\n");
                conn->running = false;
                packet_free_body(pkt);
                break;
            }
        }

        pthread_mutex_unlock(&conn->socket_mtx);

        packet_free_body(pkt);
    }

    printf("client send thread exit\n");
    pthread_exit(NULL);
}
