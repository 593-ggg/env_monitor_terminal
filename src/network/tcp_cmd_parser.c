#include "tcp_cmd_parser.h"
#include "tcp_server.h"
#include "config_manager.h"
#include "image.h"
#include "led.h"
#include "beep.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


/**
 * @brief 获取服务端上传目录（从配置读取，带尾部斜杠）
 * @return 上传目录路径字符串
 */
static const char* get_upload_dir(void)
{
    AppConfig* cfg = config_get_global();
    if (cfg && cfg->server_upload_dir[0])
        return cfg->server_upload_dir;
    return DEF_SERVER_UPLOAD_DIR;
}

/***********************************************************************
 *  服务端侧：exec_cmd_xxx 解析PKT_MSG_TEXT包体 → 执行路由
 ***********************************************************************/

/**
 * @brief 向指定连接推送一条文本消息（辅助函数）
 * @param conn 发送者连接
 * @param text 要发送的文本消息
 * @return 0成功，-1 发送失败
 */
static void send_text_to(ClientConn* conn, const char* text)
{
    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) text, strlen(text));
    client_send_enqueue(conn, &pkt);
    packet_free_body(&pkt);
}

/**
 * @brief 向指定连接发送错误应答包（PKT_ERR 类型）
 *        body 携带错误描述文本，pkt_sub_type 携带错误类型枚举值
 * @param conn 目标连接
 * @param err_type 错误类型（ERR_TYPE 枚举值，如 ERR_FILE_NOT_FOUND）
 * @param err_msg 错误描述文本
 */
static void send_error_to(ClientConn* conn, uint8_t err_type, const char* err_msg)
{
    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_ERR, err_type, (uint8_t*)err_msg, strlen(err_msg));
    client_send_enqueue(conn, &pkt);
    packet_free_body(&pkt);
}

/**
 * @brief 执行 /id_set xxxxx 命令 —— 注册/修改客户端标识
 * @param conn 发送者连接
 * @param new_id 新标识字符串
 * @return 0成功，-1 ID已被占用
 */
static int exec_cmd_set_id(ClientConn* conn, const char* new_id)
{
    if (!new_id || *new_id == '\0')
        return -1;

    int ret = client_mgr_set_id(conn, new_id);
    if (ret == 0)
    {
        printf("[%d] id set to '%s'\n", conn->fd, new_id);
        char buf[128];
        snprintf(buf, sizeof(buf), "id set ok: %s", new_id);
        send_text_to(conn, buf);
    }
    else
    {
        printf("[%d] id '%s' already taken\n", conn->fd, new_id);
        char buf[128];
        snprintf(buf, sizeof(buf), "id '%s' already in use", new_id);
        send_text_to(conn, buf);
    }
    return ret;
}

/**
 * @brief 执行私聊命令 —— 解析 @target|msg 并转发到目标客户端
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "@target|msg"
 * @return 0成功，-1目标不在线或格式错误
 */
static int exec_cmd_private_msg(ClientConn* conn, const char* body_str)
{
    // body_str 以 '@' 开头，需本地拷贝以便修改分隔符
    char buf[2048] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    char* sep = strchr(buf, '|');
    if (!sep)
        return -1;

    *sep = '\0';
    const char* target_id = buf + 1; // 跳过'@'
    const char* msg = sep + 1;

    // 构造转发消息：[发送者id]消息内容
    char fwd_buf[2048];
    snprintf(fwd_buf, sizeof(fwd_buf), "[%s]%s", conn->id, msg);

    Packet fwd_pkt;
    packet_init(&fwd_pkt);
    packet_set_body(&fwd_pkt, PKT_MSG_TEXT, CMD_MSG_PRIVATE, (uint8_t*) fwd_buf, strlen(fwd_buf));

    int ret = client_mgr_send_to_id(target_id, &fwd_pkt);
    packet_free_body(&fwd_pkt);

    if (ret == 0)
    {
        printf("[%s] -> [%s]: %s\n", conn->id, target_id, msg);
    }
    else
    {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "user '%s' not online", target_id);
        send_text_to(conn, err_buf);
    }
    return ret;
}

/**
 * @brief 执行群发广播命令 —— 向除发送者外所有客户端广播消息
 * @param conn 发送者连接
 * @param text 原始文本
 * @return 成功发送的客户端数
 */
static int exec_cmd_broadcast(ClientConn* conn, const char* text)
{
    char buf[2048];
    snprintf(buf, sizeof(buf), "[%s]: %s", conn->id, text);

    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_MSG_TEXT, CMD_MSG, (uint8_t*) buf, strlen(buf));

    int sent = client_mgr_broadcast(&pkt, conn->fd);
    packet_free_body(&pkt);

    printf("[%s] broadcast (%d clients): %s\n", conn->id, sent, text);
    return sent;
}

/**
 * @brief 确保上传目录存在
 */
static void ensure_upload_dir(void)
{
    const char* dir = get_upload_dir();
    struct stat st;
    if (stat(dir, &st) != 0)
    {
        mkdir(dir, 0755);
    }
}

/**
 * @brief 执行 /upload 命令 —— 客户端上传文件到服务端
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "filename|filesize|filepath"
 * @return 0成功
 */
static int exec_cmd_file_upload(ClientConn* conn, const char* body_str)
{
    ensure_upload_dir();

    // 解析 body: filename|filesize|filepath|server_subdir
    char buf[1024] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    char* sep1 = strchr(buf, '|');
    if (!sep1)
        return -1;
    *sep1 = '\0';
    const char* filename = buf;

    char* sep2 = strchr(sep1 + 1, '|');
    if (!sep2)
        return -1;
    *sep2 = '\0';
    uint64_t file_size = strtoull(sep1 + 1, NULL, 10);

    // 第三个字段: filepath（客户端本地路径，服务端不使用）
    const char* filepath = sep2 + 1;
    char* sep3 = strchr(filepath, '|');
    char server_subdir[256] = {0};  /* 规范化后的服务端子目录，确保以 '/' 结尾 */
    if (sep3)
    {
        *sep3 = '\0';
        const char* raw_subdir = sep3 + 1;
        // 复制到本地缓冲，便于规范化
        strncpy(server_subdir, raw_subdir, sizeof(server_subdir) - 2);
        server_subdir[sizeof(server_subdir) - 2] = '\0';
        // 非空时确保以 '/' 结尾
        size_t sub_len = strlen(server_subdir);
        if (sub_len > 0 && server_subdir[sub_len - 1] != '/')
        {
            server_subdir[sub_len] = '/';
            server_subdir[sub_len + 1] = '\0';
        }
    }

    printf("[%s] upload request: %s (%llu bytes) subdir='%s'\n",
           conn->id, filename, (unsigned long long) file_size, server_subdir);

    // 路径遍历防护：拒绝包含 ".." 或以 "/" 开头的路径
    if (strstr(filename, "..") != NULL || filename[0] == '/' ||
        strstr(server_subdir, "..") != NULL || server_subdir[0] == '/')
    {
        send_error_to(conn, ERR_PERMISSION, "invalid path: path traversal detected");
        return -1;
    }

    // 构造服务端文件路径（含子目录）
    char server_path[512];
    if (server_subdir[0] != '\0')
    {
        // 递归创建子目录
        snprintf(server_path, sizeof(server_path), "%s%s", get_upload_dir(), server_subdir);
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s", server_path);
        for (char* p = tmp + 1; *p; p++)
        {
            if (*p == '/')
            {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);

        // 拼接最终文件路径 (server_subdir 已保证以 '/' 结尾)
        snprintf(server_path, sizeof(server_path), "%s%s%s", get_upload_dir(), server_subdir, filename);
    }
    else
    {
        snprintf(server_path, sizeof(server_path), "%s%s", get_upload_dir(), filename);
    }

    // 打开文件准备写入
    int file_fd = open(server_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0)
    {
        send_error_to(conn, ERR_FILE_OPEN_FAILED, "cannot create file on server");
        return -1;
    }

    // 设置上传会话
    extern void server_upload_session_set(int uploader_fd, int file_fd, const char* filename);
    server_upload_session_set(conn->fd, file_fd, filename);

    // 状态机：进入上传状态
    conn->state = STATE_UPLOADING;

    // 回复客户端可以开始上传（使用 CMD_FILE_UPLOAD 子类型，客户端据此识别）
    Packet ready_pkt;
    packet_init(&ready_pkt);
    packet_set_body(&ready_pkt, PKT_MSG_TEXT, CMD_FILE_UPLOAD, (uint8_t*) "upload ready", 12);
    client_send_enqueue(conn, &ready_pkt);
    packet_free_body(&ready_pkt);
    return 0;
}

/**
 * @brief 执行 /download 命令 —— 客户端从服务端下载文件
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "filename|save_path"
 * @return 0成功
 */
static int exec_cmd_file_download(ClientConn* conn, const char* body_str)
{
    ensure_upload_dir();

    // 解析 body: filename|save_path
    char buf[512] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    char* sep = strchr(buf, '|');
    if (!sep)
        return -1;
    *sep = '\0';
    const char* filename = buf;

    // 路径遍历防护：拒绝包含 ".." 或以 "/" 开头的路径
    if (strstr(filename, "..") != NULL || filename[0] == '/')
    {
        send_error_to(conn, ERR_PERMISSION, "invalid path: path traversal detected");
        return -1;
    }

    // 构造服务端文件路径
    char server_path[512];
    snprintf(server_path, sizeof(server_path), "%s%s", get_upload_dir(), filename);

    // 二次验证：realpath 解析后确认仍在上传目录内
    char resolved[512];
    if (realpath(server_path, resolved) == NULL)
    {
        char err[256];
        snprintf(err, sizeof(err), "file '%s' not found on server", filename);
        send_error_to(conn, ERR_FILE_NOT_FOUND, err);
        return -1;
    }
    char upload_real[512];
    if (realpath(get_upload_dir(), upload_real) != NULL)
    {
        size_t ulen = strlen(upload_real);
        if (strncmp(resolved, upload_real, ulen) != 0 || (resolved[ulen] != '\0' && resolved[ulen] != '/'))
        {
            send_error_to(conn, ERR_PERMISSION, "invalid path: outside upload directory");
            return -1;
        }
    }

    // 检查文件是否存在
    struct stat file_stat;
    if (stat(resolved, &file_stat) != 0 || !S_ISREG(file_stat.st_mode))
    {
        char err[256];
        snprintf(err, sizeof(err), "file '%s' not found on server", filename);
        send_error_to(conn, ERR_FILE_NOT_FOUND, err);
        return -1;
    }

    // 从路径中提取纯文件名用于 META 通知
    const char* pure_name = strrchr(filename, '/');
    pure_name = pure_name ? pure_name + 1 : filename;

    printf("[%s] download request: %s (%llu bytes)\n", conn->id, filename, (unsigned long long) file_stat.st_size);

    // 发送文件元信息给客户端（直接发送，不用发送队列，保证 META→DATA→END 顺序）
    char meta[512];
    snprintf(meta, sizeof(meta), "server|%s|%llu", pure_name, (unsigned long long) file_stat.st_size);

    Packet meta_pkt;
    packet_init(&meta_pkt);
    packet_set_body(&meta_pkt, PKT_FILE_META, 0, (uint8_t*) meta, strlen(meta));

    pthread_mutex_lock(&conn->socket_mtx);
    pkt_send_full(conn->fd, &meta_pkt);
    pthread_mutex_unlock(&conn->socket_mtx);

    packet_free_body(&meta_pkt);

    // 设置会话，标记这是服务端到客户端的下载
    extern void server_file_session_set(int sender_fd, ClientConn* target);
    server_file_session_set(-1, conn); // -1 表示服务端是发送方

    // 状态机：进入文件转发状态（服务端发送文件给客户端）
    conn->state = STATE_RELAYING_FILE;

    // 打开文件并发送数据
    int fd = open(resolved, O_RDONLY);
    if (fd < 0)
    {
        conn->state = STATE_IDLE;
        send_error_to(conn, ERR_FILE_OPEN_FAILED, "cannot open file for reading");
        return -1;
    }

    uint8_t chunk[FILE_CHUNK_SIZE];
    ssize_t n;
    while ((n = read(fd, chunk, FILE_CHUNK_SIZE)) > 0)
    {
        Packet data_pkt;
        packet_init(&data_pkt);
        packet_set_body(&data_pkt, PKT_FILE_DATA, 0, chunk, n);

        pthread_mutex_lock(&conn->socket_mtx);
        pkt_send_full(conn->fd, &data_pkt);
        pthread_mutex_unlock(&conn->socket_mtx);

        packet_free_body(&data_pkt);
    }
    close(fd);

    // 发送结束包
    Packet end_pkt;
    packet_init(&end_pkt);
    packet_set_body(&end_pkt, PKT_FILE_END, 0, NULL, 0);

    pthread_mutex_lock(&conn->socket_mtx);
    pkt_send_full(conn->fd, &end_pkt);
    pthread_mutex_unlock(&conn->socket_mtx);

    packet_free_body(&end_pkt);

    // 状态机：下载完成，回到空闲
    conn->state = STATE_IDLE;
    printf("[%s] download complete: %s\n", conn->id, filename);
    return 0;
}

/**
 * @brief 递归扫描目录，将文件列表写入 result 缓冲区
 * @param dir_path 要扫描的目录路径（以 / 结尾）
 * @param rel_prefix 相对上传目录的前缀（如 "photo/" 或 ""）
 * @param result 结果缓冲区
 * @param offset 当前写入偏移
 * @param buf_size 缓冲区总大小
 * @param count 文件计数器指针
 */
static void scan_dir_recursive(const char* dir_path, const char* rel_prefix,
                               char* result, int* offset, size_t buf_size, int* count)
{
    DIR* dir = opendir(dir_path);
    if (!dir)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);

        char rel_name[512];
        snprintf(rel_name, sizeof(rel_name), "%s%s", rel_prefix, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            // 递归进入子目录
            char sub_dir[512];
            snprintf(sub_dir, sizeof(sub_dir), "%s/", full_path);
            char sub_prefix[512];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s/", rel_name);
            scan_dir_recursive(sub_dir, sub_prefix, result, offset, buf_size, count);
        }
        else if (S_ISREG(st.st_mode))
        {
            *offset += snprintf(result + *offset, buf_size - *offset,
                                "[%d] %s (%llu bytes)\n", ++(*count),
                                rel_name, (unsigned long long) st.st_size);
        }
    }
    closedir(dir);
}

/**
 * @brief 执行 /files 命令 —— 递归列出服务端所有可下载文件
 * @param conn 发送者连接
 * @return 0成功
 */
static int exec_cmd_file_list(ClientConn* conn)
{
    ensure_upload_dir();

    char result[4096] = {0};
    int offset = 0;
    int count = 0;
    offset += snprintf(result + offset, sizeof(result) - offset, "=== Files on Server ===\n");

    scan_dir_recursive(get_upload_dir(), "", result, &offset, sizeof(result), &count);

    if (count == 0)
    {
        offset += snprintf(result + offset, sizeof(result) - offset, "(no files)\n");
    }
    offset += snprintf(result + offset, sizeof(result) - offset, "=======================\n");

    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) result, strlen(result));
    client_send_enqueue(conn, &pkt);
    packet_free_body(&pkt);

    printf("[%s] file list requested (%d files)\n", conn->id, count);
    return 0;
}

/************************ 文件处理（上传后处理，后续下载结果） ************************/

/**
 * @brief YOLOv8 文件处理回调（用户在此实现实际处理逻辑）
 * @param input_path  输入文件完整路径（如 ./uploads/input.bmp）
 * @param output_path 输出文件完整路径（如 ./uploads/output_yolov8.bmp）
 * @return 0成功，-1失败
 *
 * 用户应在此函数内部调用 YOLOv8 模型对输入文件进行推理，
 * 并将结果保存到 output_path。当前为 stub 实现。
 */
int server_yolov8_process(const char* input_path, const char* output_path)
{
    int ret = image_bmp_detect_save(input_path, output_path);
    return ret;
}

// 后台处理线程参数
struct ProcessTaskArg
{
    int client_fd;
    char client_id[CLIENT_ID_LEN];
    char input_path[512];
    char output_path[512];
    char output_filename[256];
};

// 后台处理线程函数
static void* process_thread_func(void* arg)
{
    struct ProcessTaskArg* task = (struct ProcessTaskArg*) arg;

    int ret = server_yolov8_process(task->input_path, task->output_path);

    // 构造通知消息
    char notify[512];
    if (ret == 0)
    {
        snprintf(notify, sizeof(notify), "yolov8 processing complete: %s", task->output_filename);
        printf("[yolov8] processing complete: %s -> %s\n", task->input_path, task->output_path);
    }
    else
    {
        snprintf(notify, sizeof(notify), "yolov8 processing failed: %s", task->output_filename);
        printf("[yolov8] processing failed: %s -> %s\n", task->input_path, task->output_path);
    }

    // 通过 client_mgr_find_by_id 查找客户端通知
    // 如果客户端已断开，通知会失败（忽略）
    Packet pkt;
    packet_init(&pkt);
    packet_set_body(&pkt, PKT_MSG_TEXT, CMD_TXT, (uint8_t*) notify, strlen(notify));
    client_mgr_send_to_id(task->client_id, &pkt);
    packet_free_body(&pkt);

    free(task);
    return NULL;
}

/**
 * @brief 执行 /yolov8 命令 —— YOLOv8 处理文件
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "input_filename|output_filename"
 * @return 0成功
 */
static int exec_cmd_yolov8(ClientConn* conn, const char* body_str)
{
    ensure_upload_dir();

    // 解析 body: input_filename|output_filename
    char buf[512] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    char* sep = strchr(buf, '|');
    if (!sep)
        return -1;
    *sep = '\0';
    const char* input_filename = buf;
    const char* output_filename = sep + 1;

    if (*input_filename == '\0' || *output_filename == '\0')
        return -1;

    // 路径遍历防护：拒绝包含 ".." 或以 "/" 开头的路径
    if (strstr(input_filename, "..") != NULL || input_filename[0] == '/' ||
        strstr(output_filename, "..") != NULL || output_filename[0] == '/')
    {
        send_error_to(conn, ERR_PERMISSION, "invalid path: path traversal detected");
        return -1;
    }

    // 检查输入文件是否存在
    char input_path[512];
    snprintf(input_path, sizeof(input_path), "%s%s", get_upload_dir(), input_filename);

    struct stat st;
    if (stat(input_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        char err[256];
        snprintf(err, sizeof(err), "input file '%s' not found on server", input_filename);
        send_text_to(conn, err);
        return -1;
    }

    // 检查输出文件是否已存在
    char output_path[512];
    snprintf(output_path, sizeof(output_path), "%s%s", get_upload_dir(), output_filename);

    if (stat(output_path, &st) == 0)
    {
        char err[256];
        snprintf(err, sizeof(err), "output file '%s' already exists", output_filename);
        send_text_to(conn, err);
        return -1;
    }

    // 创建后台线程处理（不阻塞当前连接）
    struct ProcessTaskArg* task = malloc(sizeof(struct ProcessTaskArg));
    if (!task)
        return -1;

    task->client_fd = conn->fd;
    strncpy(task->client_id, conn->id, CLIENT_ID_LEN - 1);
    task->client_id[CLIENT_ID_LEN - 1] = '\0';
    strncpy(task->input_path, input_path, sizeof(task->input_path) - 1);
    task->input_path[sizeof(task->input_path) - 1] = '\0';
    strncpy(task->output_path, output_path, sizeof(task->output_path) - 1);
    task->output_path[sizeof(task->output_path) - 1] = '\0';
    strncpy(task->output_filename, output_filename, sizeof(task->output_filename) - 1);
    task->output_filename[sizeof(task->output_filename) - 1] = '\0';

    pthread_t tid;
    if (pthread_create(&tid, NULL, process_thread_func, task) != 0)
    {
        free(task);
        send_text_to(conn, "yolov8 processing failed: cannot create thread");
        return -1;
    }
    pthread_detach(tid);

    // 立即回复客户端
    char reply[256];
    snprintf(reply, sizeof(reply), "yolov8 processing started: %s -> %s", input_filename, output_filename);
    send_text_to(conn, reply);

    printf("[%s] yolov8 processing started: %s -> %s\n", conn->id, input_filename, output_filename);
    return 0;
}

/************************ 客户端之间文件传输 ************************/

/**
 * @brief 执行 /file 命令 —— 转发文件元信息给目标客户端
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "@target_id|filepath|filesize"
 * @return 0成功
 */
static int exec_cmd_file_put(ClientConn* conn, const char* body_str)
{
    // 格式: @target_id|filepath|filesize
    char buf[2048] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    if (buf[0] != '@')
        return -1;
    char* sep1 = strchr(buf + 1, '|');
    if (!sep1)
        return -1;
    *sep1 = '\0';
    const char* target_id = buf + 1;
    const char* rest = sep1 + 1;

    char* sep2 = strchr(rest, '|');
    if (!sep2)
        return -1;
    *sep2 = '\0';
    const char* filepath = rest;
    uint64_t file_size = strtoull(sep2 + 1, NULL, 10);

    ClientConn* target = client_mgr_find_by_id(target_id);
    if (!target)
    {
        char err[128];
        snprintf(err, sizeof(err), "user '%s' not online", target_id);
        send_text_to(conn, err);
        return -1;
    }

    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    char meta[512];
    snprintf(meta, sizeof(meta), "%s|%s|%llu", conn->id, filename, (unsigned long long) file_size);

    Packet meta_pkt;
    packet_init(&meta_pkt);
    packet_set_body(&meta_pkt, PKT_FILE_META, 0, (uint8_t*) meta, strlen(meta));
    client_send_enqueue(target, &meta_pkt);
    packet_free_body(&meta_pkt);
    extern void server_file_session_set(int sender_fd, ClientConn* target);
    server_file_session_set(conn->fd, target);

    // 状态机：发送方进入文件转发状态
    conn->state = STATE_RELAYING_FILE;
    send_text_to(conn, "file meta sent, waiting for target to accept...");
    return 0;
}

/**
 * @brief 执行 file_accept 命令 —— 转发文件接受通知给发送者
 * @param conn 接收者连接
 * @param body_str body字符串，格式为 "sender_id|save_path"
 * @return 0成功
 */
static int exec_cmd_file_accept(ClientConn* conn, const char* body_str)
{
    char buf[256] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    char* sep = strchr(buf, '|');
    if (!sep)
        return -1;
    *sep = '\0';
    const char* sender_id = buf;
    const char* save_path = sep + 1;

    ClientConn* sender = client_mgr_find_by_id(sender_id);
    if (!sender)
    {
        printf("[CMD_FILE_ACCEPT] sender '%s' not found!\n", sender_id);
        return -1;
    }

    char accept_buf[256];
    snprintf(accept_buf, sizeof(accept_buf), "%s|%s", conn->id, save_path);
    Packet accept_pkt;
    packet_init(&accept_pkt);
    packet_set_body(&accept_pkt, PKT_MSG_TEXT, CMD_FILE_ACCEPT, (uint8_t*) accept_buf, strlen(accept_buf));
    client_send_enqueue(sender, &accept_pkt);
    packet_free_body(&accept_pkt);
    return 0;
}

/**
 * @brief 执行 file_reject 命令 —— 转发文件拒绝通知给发送者
 * @param conn 接收者连接
 * @param body_str body字符串，格式为 "sender_id"
 * @return 0成功
 */
static int exec_cmd_file_reject(ClientConn* conn, const char* body_str)
{
    ClientConn* sender = client_mgr_find_by_id(body_str);
    if (!sender)
        return -1;

    char reject_buf[128];
    snprintf(reject_buf, sizeof(reject_buf), "%s rejected the file", conn->id);
    Packet reject_pkt;
    packet_init(&reject_pkt);
    packet_set_body(&reject_pkt, PKT_MSG_TEXT, CMD_FILE_REJECT, (uint8_t*) reject_buf, strlen(reject_buf));
    client_send_enqueue(sender, &reject_pkt);
    packet_free_body(&reject_pkt);
    return 0;
}

/************************ 硬件设备控制 ************************/

/**
 * @brief 执行 /led 命令 —— 控制 LED 灯开关
 * @param conn 发送者连接
 * @param body_str body字符串，格式为 "idx|action"，idx=0-3，action=on/off
 * @return 0成功，-1参数错误
 */
static int exec_cmd_led(ClientConn* conn, const char* body_str)
{
    if (!body_str || *body_str == '\0')
    {
        send_text_to(conn, "led cmd format: idx|on|off");
        return -1;
    }

    // 拷贝到本地缓冲区以便解析
    char buf[64] = {0};
    strncpy(buf, body_str, sizeof(buf) - 1);

    // 解析 "idx|action"
    char* sep = strchr(buf, '|');
    if (!sep)
    {
        send_text_to(conn, "led cmd format error, use: led idx on/off");
        return -1;
    }
    *sep = '\0';

    int idx = atoi(buf);
    const char* action = sep + 1;

    // 校验 LED 编号
    if (idx < 0 || idx >= LED_MAX)
    {
        char err[64];
        snprintf(err, sizeof(err), "led idx %d invalid, must be 0-%d", idx, LED_MAX - 1);
        send_text_to(conn, err);
        return -1;
    }

    // 执行开关动作
    if (strcmp(action, "on") == 0)
    {
        device_led_on((led_idx_t) idx);
    }
    else if (strcmp(action, "off") == 0)
    {
        device_led_off((led_idx_t) idx);
    }
    else
    {
        char err[64];
        snprintf(err, sizeof(err), "led action '%s' invalid, must be on/off", action);
        send_text_to(conn, err);
        return -1;
    }

    // 回复操作结果
    char reply[64];
    snprintf(reply, sizeof(reply), "led %d %s ok", idx, action);
    send_text_to(conn, reply);

    printf("[%s] led %d %s\n", conn->id, idx, action);
    return 0;
}

/**
 * @brief 执行 /beep 命令 —— 控制蜂鸣器开关
 * @param conn 发送者连接
 * @param body_str body字符串，内容为 "on" 或 "off"
 * @return 0成功，-1参数错误
 */
static int exec_cmd_beep(ClientConn* conn, const char* body_str)
{
    if (!body_str || *body_str == '\0')
    {
        send_text_to(conn, "beep cmd format: on/off");
        return -1;
    }

    if (strcmp(body_str, "on") == 0)
    {
        device_beep_on();
    }
    else if (strcmp(body_str, "off") == 0)
    {
        device_beep_off();
    }
    else
    {
        char err[64];
        snprintf(err, sizeof(err), "beep action '%s' invalid, must be on/off", body_str);
        send_text_to(conn, err);
        return -1;
    }

    // 回复操作结果
    char reply[64];
    snprintf(reply, sizeof(reply), "beep %s ok", body_str);
    send_text_to(conn, reply);

    printf("[%s] beep %s\n", conn->id, body_str);
    return 0;
}

/************************ 命令分发器 ************************/

/**
 * @brief 服务端命令分发入口
 * @param conn 发送者连接
 * @param pkt  收到的数据包
 * @return 0成功，-1失败
 */
int cmd_server_dispatch(ClientConn* conn, Packet* pkt)
{
    if (!conn || !pkt)
        return -1;

    // 拷贝body到本地缓冲区，确保'\0'结尾
    char buf[2048] = {0};
    uint32_t copy_len = (pkt->body_len >= sizeof(buf)) ? (sizeof(buf) - 1) : pkt->body_len;
    memcpy(buf, pkt->body, copy_len);
    buf[copy_len] = '\0';

    enum CMD_TYPE cmd = pkt->hdr.pkt_sub_type;

    // ========== 状态机校验：仅对文件类命令做状态保护 ==========
    // 文件传输 / 上传 / 下载 命令仅在 IDLE 状态下允许
    if (cmd == CMD_FILE_PUT || cmd == CMD_FILE_UPLOAD || cmd == CMD_FILE_DOWNLOAD) {
        if (conn->state != STATE_IDLE) {
            char err[128];
            snprintf(err, sizeof(err), "busy, current state: %d", conn->state);
            send_text_to(conn, err);
            return -1;
        }
    }

    // 从pkt_sub_type取出命令类型并分发
    switch (cmd)
    {
        case CMD_SET_ID:
            return exec_cmd_set_id(conn, buf);
        case CMD_MSG_PRIVATE:
            return exec_cmd_private_msg(conn, buf);
        case CMD_MSG:
            exec_cmd_broadcast(conn, buf);
            return 0;
        case CMD_CLIENT_LIST:
            client_mgr_list_to(conn);
            return 0;

        case CMD_FILE_PUT:
            return exec_cmd_file_put(conn, buf);
        case CMD_FILE_ACCEPT:
            return exec_cmd_file_accept(conn, buf);
        case CMD_FILE_REJECT:
            return exec_cmd_file_reject(conn, buf);

        case CMD_FILE_UPLOAD:
            return exec_cmd_file_upload(conn, buf);
        case CMD_FILE_DOWNLOAD:
            return exec_cmd_file_download(conn, buf);
        case CMD_FILE_LIST:
            return exec_cmd_file_list(conn);
        case CMD_YOLOV8:
            return exec_cmd_yolov8(conn, buf);

        case CMD_LED:
            return exec_cmd_led(conn, buf);
        case CMD_BEEP:
            return exec_cmd_beep(conn, buf);

        default:
            return -1;
    }
}
