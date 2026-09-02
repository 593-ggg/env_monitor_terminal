#include "thread.h"
#include "GY39.h"
#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ==================== GY39 线程配置 ==================== */
#define GY39_UART_DEV       UART4       /* GY39 接在 UART4 */
#define GY39_BAUDRATE       9600        /* GY39 默认波特率 */
#define GY39_RECV_BUF_SIZE  64          /* 串口接收缓冲区大小 */
#define GY39_POLL_INTERVAL  10000       /* 轮询间隔 (微秒) */

/* ==================== 线程状态 ==================== */
static pthread_t gy39_thread_id = 0;
static volatile bool gy39_thread_run = false;

/* 串口 fd */
static int gy39_fd = -1;

/* 最新传感器数据 (加锁保护) */
static Gy39LuxData latest_lux = {0};
static Gy39EnvData latest_env = {0};
static pthread_mutex_t gy39_data_lock = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 线程入口函数 ==================== */

static void *gy39_thread_entry(void *arg)
{
    (void) arg;

    printf("[GY39_THREAD] read thread start, poll interval %dus\n", GY39_POLL_INTERVAL);

    char recv_buf[GY39_RECV_BUF_SIZE] = {0};
    int buf_idx = 0;
    unsigned char one_byte;

    while (gy39_thread_run)
    {
        int ret_read = serial_recv(gy39_fd, (char *)&one_byte, 1);
        if (ret_read <= 0)
        {
            usleep(GY39_POLL_INTERVAL);
            continue;
        }

        /* 收到字节存入缓冲区 */
        recv_buf[buf_idx++] = one_byte;

        /* 检测帧头 0x5A 0x5A, 重置缓冲区 */
        if (buf_idx >= 2 && recv_buf[buf_idx - 2] == GY39_HEAD0 && recv_buf[buf_idx - 1] == GY39_HEAD1)
            buf_idx = 2;

        /* 缓冲区防溢出 */
        if (buf_idx >= GY39_RECV_BUF_SIZE)
            buf_idx = 0;

        /* 尝试解析 */
        Gy39LuxData lux_data;
        Gy39EnvData env_data;
        int res = gy39_parse_frame((unsigned char *)recv_buf, buf_idx, &lux_data, &env_data);

        if (res == 1)
        {
            pthread_mutex_lock(&gy39_data_lock);
            latest_lux = lux_data;
            pthread_mutex_unlock(&gy39_data_lock);
            buf_idx = 0;
        }
        else if (res == 2)
        {
            pthread_mutex_lock(&gy39_data_lock);
            latest_env = env_data;
            pthread_mutex_unlock(&gy39_data_lock);
            buf_idx = 0;
        }
    }

    printf("[GY39_THREAD] read thread exit\n");
    return NULL;
}

/* ==================== 公共接口实现 ==================== */

int gy39_thread_start(void)
{
    if (gy39_thread_run)
    {
        printf("[GY39_THREAD] thread already running\n");
        return 0;
    }

    /* 1. 打开串口 */
    gy39_fd = init_serial(GY39_UART_DEV, GY39_BAUDRATE);
    if (gy39_fd < 0)
    {
        printf("[GY39_THREAD] serial open failed\n");
        return -1;
    }

    /* 2. 配置 GY39 自动上报 */
    unsigned char cfg = GY39_AUTO_EN | GY39_BME_EN | GY39_MAX_EN;
    if (gy39_set_auto_output(gy39_fd, cfg) != 0)
    {
        printf("[GY39_THREAD] set auto output failed\n");
        serial_close(gy39_fd);
        gy39_fd = -1;
        return -1;
    }

    /* 3. 清空初始数据 */
    memset(&latest_lux, 0, sizeof(latest_lux));
    memset(&latest_env, 0, sizeof(latest_env));

    gy39_thread_run = true;

    /* 4. 创建读取线程 */
    if (pthread_create(&gy39_thread_id, NULL, gy39_thread_entry, NULL) != 0)
    {
        printf("[GY39_THREAD] pthread_create failed\n");
        gy39_thread_run = false;
        serial_close(gy39_fd);
        gy39_fd = -1;
        return -1;
    }

    printf("[GY39_THREAD] start success\n");
    return 0;
}

void gy39_thread_stop(void)
{
    if (!gy39_thread_run)
        return;

    gy39_thread_run = false;

    if (gy39_thread_id != 0)
    {
        pthread_join(gy39_thread_id, NULL);
        gy39_thread_id = 0;
    }

    if (gy39_fd >= 0)
    {
        serial_close(gy39_fd);
        gy39_fd = -1;
    }

    printf("[GY39_THREAD] stop ok\n");
}

bool gy39_thread_is_running(void)
{
    return gy39_thread_run;
}

int gy39_get_lux_data(Gy39LuxData *out)
{
    if (!out)
        return -1;

    pthread_mutex_lock(&gy39_data_lock);
    *out = latest_lux;
    pthread_mutex_unlock(&gy39_data_lock);

    return 0;
}

int gy39_get_env_data(Gy39EnvData *out)
{
    if (!out)
        return -1;

    pthread_mutex_lock(&gy39_data_lock);
    *out = latest_env;
    pthread_mutex_unlock(&gy39_data_lock);

    return 0;
}

/*
// 启动 GY39 线程 (在 cam_thread_start() 旁边加)
gy39_thread_start();

// 在定时器回调中读取数据 (UI 线程安全调用)
Gy39LuxData lux;
Gy39EnvData env;
gy39_get_lux_data(&lux);
gy39_get_env_data(&env);
// 然后用 lv_label_set_text() 刷新显示
lv_label_set_text(lux_label, "%d", lux.lux);
lv_label_set_text(env_label, "%d", env.temp);
*/
