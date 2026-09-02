#include "tcp_server.h"
#include "thread.h"
#include <pthread.h>

// 服务器是否在运行
static bool server_is_running = false;
// 服务器主线程ID
static pthread_t server_tid;

/**
 * @brief 服务器主线程入口，阻塞在accept循环
 */
static void* server_thread_entry(void* arg)
{
    (void)arg;
    server_start();
    server_is_running = false;
    return NULL;
}

static int tcp_server_start(void)
{
    if (server_is_running)
        return -1;

    server_is_running = true;

    // 创建独立线程运行server_start()，避免阻塞UI线程
    if (pthread_create(&server_tid, NULL, server_thread_entry, NULL) != 0)
    {
        server_is_running = false;
        printf("tcp_server_start: create thread fail\n");
        return -1;
    }
    pthread_detach(server_tid);
    return 0;
}

static void tcp_server_close(void)
{
    if (!server_is_running)
        return;

    server_is_running = false;
    // 关闭listen fd，使accept()返回失败跳出while(1)循环
    server_shutdown();
    // 线程已detach，无需join
}

bool tcp_server_is_running(void)
{
    return server_is_running;
}

// 解析ui层指令（无参数命令：启动/关闭服务器）
int tcp_server_parse_ui_cmd(enum TcpUICmd cmd)
{
    switch (cmd)
    {
        case TCP_UI_CMD_START:
            if (server_is_running)
                return -1;
            tcp_server_start();
            break;
        case TCP_UI_CMD_CLOSE:
            if (!server_is_running)
                return -1;
            tcp_server_close();
            break;
        case TCP_UI_CMD_DISCONNECT:
            // DISCONNECT需要带参数，通过tcp_server_disconnect_client调用
            printf("tcp_server_parse_ui_cmd: DISCONNECT needs client_id, use tcp_server_disconnect_client()\n");
            return -1;
        default:
            printf("tcp_server_parse_ui_cmd: unknown cmd %d\n", cmd);
            return -1;
    }
    return 0;
}

int tcp_server_disconnect_client(const char* client_id)
{
    if (!server_is_running)
    {
        printf("tcp_server_disconnect_client: server is not running\n");
        return -1;
    }
    if (!client_id || *client_id == '\0')
        return -1;

    client_mgr_disconnect(client_id);
    return 0;
}
