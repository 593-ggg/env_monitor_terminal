#include "tcp_cmd_parser.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/***********************************************************************
 *  客户端侧：parse_cmd_xxx 解析终端命令 → 构造Packet
 ***********************************************************************/

/**
 * @brief 处理 /id_set xxx 设置客户端标识命令（客户端侧构造包体）
 * @param id_str /id_set 后的标识字符串
 * @param out_pkt 输出数据包
 * @param pkt_sub_type 消息子类型
 * @return 0成功，-1参数非法
 */
static int parse_cmd_set_id(const char* id_str, Packet* out_pkt, uint8_t pkt_sub_type)
{
    if (!id_str || *id_str == '\0')
        return -1;
    return packet_set_body(out_pkt, PKT_MSG_TEXT, pkt_sub_type, (uint8_t*) id_str, strlen(id_str));
}

/**
 * @brief 处理 /msg xxx 广播消息文本命令
 * @param arg_str /msg 后的参数字符串
 * @param out_pkt 输出数据包
 * @param pkt_sub_type 消息子类型
 * @return 同packet_from_cmdline错误码
 */
static int parse_cmd_msg(const char* arg_str, Packet* out_pkt, uint8_t pkt_sub_type)
{
    if (!arg_str || *arg_str == '\0')
        return -1;

    return packet_set_body(out_pkt, PKT_MSG_TEXT, pkt_sub_type, (uint8_t*) arg_str, strlen(arg_str));
}

/**
 * @brief 处理 /msg @user 消息文本命令（客户端侧构造包体）
 * @param arg_str /msg 后的参数字符串
 * @param out_pkt 输出数据包
 * @param pkt_sub_type 消息子类型
 * @return 同packet_from_cmdline错误码
 */
static int parse_cmd_msg_private(const char* arg_str, Packet* out_pkt, uint8_t pkt_sub_type)
{
    if (!arg_str || *arg_str == '\0')
        return -1;

    // 查找@接收人标记
    char* at_ptr = strchr(arg_str, '@');
    if (!at_ptr)
        return -1;

    // 空格分割用户名与消息内容
    char* space_ptr = strchr(at_ptr, ' ');
    if (!space_ptr)
        return -1;

    // 拼接协议body格式：@接收人|消息
    char body_buf[512] = {0};
    size_t user_len = space_ptr - at_ptr;
    strncat(body_buf, at_ptr, user_len);
    strcat(body_buf, "|");
    strcat(body_buf, space_ptr + 1);

    return packet_set_body(out_pkt, PKT_MSG_TEXT, pkt_sub_type, (uint8_t*) body_buf, strlen(body_buf));
}

/**
 * @brief 处理 /heartbeat 心跳命令（客户端侧构造包体）
 * @param out_pkt 输出数据包
 * @param pkt_sub_type 消息子类型
 @return 0成功
 */
static int parse_cmd_heartbeat(Packet* out_pkt, uint8_t pkt_sub_type)
{
    return packet_set_body(out_pkt, PKT_HEARTBEAT, pkt_sub_type, NULL, 0);
}

/**
 * @brief 解析命令字符串，映射到CLI_CMD枚举
 * @param buf 可修改的命令缓冲区（会被strtok_r修改）
 * @param args_out 输出参数指针，指向命令关键字后的参数部分，无参数时为NULL
 * @return CLI_CMD枚举值
 */
static enum CMD_TYPE parse_cli_cmd(char* buf, const char** args_out)
{
    *args_out = NULL;

    // 保存原始缓冲区起始地址，strtok_r 会修改 buf
    char* orig = buf;

    // 分割命令关键字与参数（空格分隔）
    char* rest = NULL;
    char* token = strtok_r(buf, " ", &rest);
    if (!token)
        return CMD_UNKNOWN;

    // 未匹配任何命令关键字 → 当作广播消息（使用原始 buf，不受 strtok_r 影响）
    // 注：strtok_r 会修改 buf（空格变\0），所以用 orig 取完整原文
    if (strcmp(token, "id") == 0)
    {
        *args_out = rest;
        return CMD_SET_ID;
    }
    if (strcmp(token, "msg") == 0)
    {
        *args_out = rest;
        if (rest && *rest == '@')
            return CMD_MSG_PRIVATE;
        else
            return CMD_MSG;
    }
    if (strcmp(token, "file") == 0)
    {
        *args_out = rest;
        return CMD_FILE_PUT;
    }
    if (strcmp(token, "file_accept") == 0)
    {
        *args_out = rest;
        return CMD_FILE_ACCEPT;
    }
    if (strcmp(token, "file_reject") == 0)
    {
        *args_out = rest;
        return CMD_FILE_REJECT;
    }
    if (strcmp(token, "heartbeat") == 0)
    {
        *args_out = rest;
        return CMD_HEARTBEAT;
    }
    if (strcmp(token, "list") == 0)
    {
        *args_out = rest;
        return CMD_CLIENT_LIST;
    }
    if (strcmp(token, "upload") == 0)
    {
        *args_out = rest;
        return CMD_FILE_UPLOAD;
    }
    if (strcmp(token, "download") == 0)
    {
        *args_out = rest;
        return CMD_FILE_DOWNLOAD;
    }
    if (strcmp(token, "files") == 0)
    {
        *args_out = rest;
        return CMD_FILE_LIST;
    }
    if (strcmp(token, "yolov8") == 0)
    {
        *args_out = rest;
        return CMD_YOLOV8;
    }
    if (strcmp(token, "led") == 0)
    {
        *args_out = rest;
        return CMD_LED;
    }
    if (strcmp(token, "beep") == 0)
    {
        *args_out = rest;
        return CMD_BEEP;
    }

    // 未匹配 → 当作未知命令
    *args_out = orig;
    return CMD_UNKNOWN;
}

/************************ 文件命令解析函数 ************************/

/**
 * @brief 解析 /file @target filepath 命令
 */
static int parse_cmd_file_put(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args != '@')
        return -1;
    const char* target_end = strchr(args, ' ');
    if (!target_end)
        return -1;

    char target_id[64];
    size_t target_len = target_end - args - 1;
    if (target_len >= sizeof(target_id))
        return -1;
    memcpy(target_id, args + 1, target_len);
    target_id[target_len] = '\0';

    const char* filepath_start = target_end + 1;
    if (*filepath_start == '\0')
        return -1;
    size_t path_len = strlen(filepath_start);
    while (path_len > 0 && filepath_start[path_len - 1] == ' ')
        path_len--;
    if (path_len == 0)
        return -1;

    char filepath[512];
    memcpy(filepath, filepath_start, path_len);
    filepath[path_len] = '\0';

    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0 || !S_ISREG(file_stat.st_mode))
        return -3;

    char body[1024];
    snprintf(body, sizeof(body), "@%s|%s|%llu", target_id, filepath, (unsigned long long) file_stat.st_size);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/**
 * @brief 解析 file_accept [save_path] 命令
 */
static int parse_cmd_file_accept(const char* args, Packet* out_pkt, uint8_t cmd)
{
    const char* save_path = (args && *args != '\0') ? args : "./";
    extern char g_file_sender_id[64];
    char body[512];
    snprintf(body, sizeof(body), "%s|%s", g_file_sender_id, save_path);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/**
 * @brief 解析 file_reject 命令
 */
static int parse_cmd_file_reject(Packet* out_pkt, uint8_t cmd)
{
    extern char g_file_sender_id[64];
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) g_file_sender_id, strlen(g_file_sender_id));
}

/**
 * @brief 解析 /upload filepath [server_subdir] 命令
 *        server_subdir 可选，指定服务端保存子目录（如 photo/），默认为根目录
 */
static int parse_cmd_file_upload(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args == '\0')
        return -1;

    // 查找第二个参数（服务端子目录）
    const char* space = strchr(args, ' ');
    char filepath[512];
    const char* server_subdir = "";

    if (space)
    {
        size_t fp_len = space - args;
        if (fp_len >= sizeof(filepath))
            return -1;
        memcpy(filepath, args, fp_len);
        filepath[fp_len] = '\0';

        // 跳过空格
        server_subdir = space + 1;
        while (*server_subdir == ' ')
            server_subdir++;
        if (*server_subdir == '\0')
            server_subdir = "";
    }
    else
    {
        size_t path_len = strlen(args);
        while (path_len > 0 && args[path_len - 1] == ' ')
            path_len--;
        if (path_len == 0)
            return -1;
        memcpy(filepath, args, path_len);
        filepath[path_len] = '\0';
    }

    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0 || !S_ISREG(file_stat.st_mode))
        return -3;

    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    char body[1024];
    snprintf(body, sizeof(body), "%s|%llu|%s|%s", filename,
             (unsigned long long) file_stat.st_size, filepath, server_subdir);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/**
 * @brief 解析 /download filename [save_path] 命令
 */
static int parse_cmd_file_download(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args == '\0')
        return -1;

    const char* filename_end = strchr(args, ' ');
    char filename[256];
    const char* save_path = "./";

    if (filename_end)
    {
        size_t fname_len = filename_end - args;
        if (fname_len >= sizeof(filename))
            return -1;
        memcpy(filename, args, fname_len);
        filename[fname_len] = '\0';
        save_path = filename_end + 1;
        while (*save_path == ' ')
            save_path++;
        if (*save_path == '\0')
            save_path = "./";
    }
    else
    {
        strncpy(filename, args, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    char body[512];
    snprintf(body, sizeof(body), "%s|%s", filename, save_path);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/**
 * @brief 解析 /yolov8 input [output] 命令
 *        output 可选，未指定时自动在原文件名后加 _yolov8
 *        约束1：输入只能是纯文件名，不能携带路径（不能包含 '/'）
 *        约束2：输入文件后缀必须为 .bmp/.BMP，否则直接返回 -1
 *        自动生成输出文件：丢弃原始路径，保存到程序当前工作目录
 *        成功/失败均打印日志
 */
static int parse_cmd_yolov8(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args == '\0')
    {
        printf("yolov8 input is null\n");
        return -1;
    }

    // 将 args 拷贝到本地缓冲区以便修改
    char buf[512] = {0};
    strncpy(buf, args, sizeof(buf) - 1);

    // 查找输入文件名与输出文件名的分隔符（空格）
    char* sep = strchr(buf, ' ');

    const char* input_name = buf;
    const char* output_name = NULL;

    if (sep)
    {
        *sep = '\0';
        output_name = sep + 1;
        // 跳过多余空格
        while (*output_name == ' ')
            output_name++;
        if (*output_name == '\0')
            output_name = NULL;
    }

    if (strchr(input_name, '/'))
    {
        printf("yolov8 input is not a pure filename, it contains path\n");
        return -1;
    }

    // 此时input_name就是纯文件名
    const char* basename = input_name;
    const char* dot = strrchr(basename, '.');

    // --------------------------
    // 严格校验后缀 .bmp / .BMP
    // --------------------------
    if (!dot)
    {
        printf("yolov8 input is missing bmp\n");
        return -1;
    }
    if ((strcmp(dot, ".bmp") != 0) && (strcmp(dot, ".BMP") != 0))
    {
        printf("yolov8 input is not bmp file\n");
        return -1;
    }

    // 未指定输出名时自动生成：在原文件名后加 _yolov8
    char auto_output[256] = {0};
    if (!output_name)
    {
        const char add_str[] = "_yolov8";
        size_t add_len = strlen(add_str);

        size_t base_len = dot - basename;
        size_t ext_len = strlen(dot);
        if (base_len + add_len + ext_len < sizeof(auto_output))
        {
            memcpy(auto_output, basename, base_len);
            memcpy(auto_output + base_len, add_str, add_len);
            memcpy(auto_output + base_len + add_len, dot, ext_len);
            auto_output[base_len + add_len + ext_len] = '\0';
        }
        output_name = auto_output;
    }

    char body[512];
    snprintf(body, sizeof(body), "%s|%s", input_name, output_name);
    printf("[YOLV8_CMD_OK] input=%s output=%s\n", input_name, output_name);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/************************ 硬件设备控制命令解析 ************************/

/**
 * @brief 解析 /led idx on|off 命令
 * @param args 命令参数字符串，格式为 "idx action"，如 "0 on" / "3 off"
 * @param out_pkt 输出数据包
 * @param cmd 命令子类型 CMD_LED
 * @return 0成功，-1参数非法
 *
 * 协议body格式: "idx|action"，如 "0|on"
 */
static int parse_cmd_led(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args == '\0')
    {
        printf("led cmd format: led idx on/off  (idx 0-3)\n");
        return -1;
    }

    // 拷贝到本地缓冲区以便 strtok 修改
    char buf[64] = {0};
    strncpy(buf, args, sizeof(buf) - 1);

    // 拆分 idx 和 action (空格分隔)
    char* rest = NULL;
    char* idx_str = strtok_r(buf, " ", &rest);
    if (!idx_str)
    {
        printf("led cmd format error, use: led idx on/off\n");
        return -1;
    }

    const char* action = rest;
    if (!action || *action == '\0')
    {
        printf("led cmd missing action, use: led idx on/off\n");
        return -1;
    }

    // 去掉 action 末尾可能的空格
    size_t act_len = strlen(action);
    while (act_len > 0 && (action[act_len - 1] == ' ' || action[act_len - 1] == '\t'))
        act_len--;

    // 校验 idx 范围
    int idx = atoi(idx_str);
    if (idx < 0 || idx > 3)
    {
        printf("led idx %d invalid, must be 0-3\n", idx);
        return -1;
    }

    // 校验 action 合法性 (on/off)
    if (act_len != 2 || strncmp(action, "on", 2) != 0)
    {
        if (act_len != 3 || strncmp(action, "off", 3) != 0)
        {
            printf("led action invalid, must be on/off\n");
            return -1;
        }
    }

    // 构造 body: "idx|action"
    char body[32];
    snprintf(body, sizeof(body), "%d|%.*s", idx, (int) act_len, action);
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) body, strlen(body));
}

/**
 * @brief 解析 /beep on|off 命令
 * @param args 命令参数字符串，内容为 "on" 或 "off"
 * @param out_pkt 输出数据包
 * @param cmd 命令子类型 CMD_BEEP
 * @return 0成功，-1参数非法
 *
 * 协议body格式: "on" / "off"
 */
static int parse_cmd_beep(const char* args, Packet* out_pkt, uint8_t cmd)
{
    if (!args || *args == '\0')
    {
        printf("beep cmd format: beep on/off\n");
        return -1;
    }

    // 去掉末尾可能的空格
    size_t act_len = strlen(args);
    while (act_len > 0 && (args[act_len - 1] == ' ' || args[act_len - 1] == '\t'))
        act_len--;

    if (act_len == 0)
    {
        printf("beep cmd missing action, use: beep on/off\n");
        return -1;
    }

    // 校验 action 合法性
    if (act_len != 2 || strncmp(args, "on", 2) != 0)
    {
        if (act_len != 3 || strncmp(args, "off", 3) != 0)
        {
            printf("beep action invalid, must be on/off\n");
            return -1;
        }
    }

    // body 直接是 action 字符串
    return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, (uint8_t*) args, act_len);
}

/************************ 命令分发器 ************************/

int packet_from_cmd(const char* cmdline, Packet* out_pkt)
{
    if (!cmdline || !out_pkt)
        return -1;
    packet_init(out_pkt);

    char buf[1024] = {0};
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    const char* args = NULL;
    enum CMD_TYPE cmd = parse_cli_cmd(buf, &args);

    switch (cmd)
    {
        case CMD_SET_ID:
            return parse_cmd_set_id(args, out_pkt, cmd);
        case CMD_MSG:
            return parse_cmd_msg(args, out_pkt, cmd);
        case CMD_MSG_PRIVATE:
            return parse_cmd_msg_private(args, out_pkt, cmd);
        case CMD_HEARTBEAT:
            return parse_cmd_heartbeat(out_pkt, cmd);

        case CMD_FILE_PUT:
            return parse_cmd_file_put(args, out_pkt, cmd);
        case CMD_FILE_ACCEPT:
            return parse_cmd_file_accept(args, out_pkt, cmd);
        case CMD_FILE_REJECT:
            return parse_cmd_file_reject(out_pkt, cmd);
        case CMD_CLIENT_LIST:
            return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, NULL, 0);

        case CMD_FILE_UPLOAD:
            return parse_cmd_file_upload(args, out_pkt, cmd);
        case CMD_FILE_DOWNLOAD:
            return parse_cmd_file_download(args, out_pkt, cmd);
        case CMD_FILE_LIST:
            return packet_set_body(out_pkt, PKT_MSG_TEXT, cmd, NULL, 0);
        case CMD_YOLOV8:
            return parse_cmd_yolov8(args, out_pkt, cmd);

        case CMD_LED:
            return parse_cmd_led(args, out_pkt, cmd);
        case CMD_BEEP:
            return parse_cmd_beep(args, out_pkt, cmd);

        default:
            return -1;
    }
}