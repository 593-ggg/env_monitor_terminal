#ifdef __linux__
#include <stdio.h>
#include <string.h>
#include "led.h"
#include "gpio_util.h"

// 硬件引脚映射
static const int led_gpio_table[LED_MAX] = {
    120,    // LED_0
    121,    // LED_1
    123,    // LED_2
    124     // LED_3
};

// 全局状态缓存：记录每一盏灯当前0/1状态
static uint8_t g_led_state[LED_MAX] = {0};

void device_led_init_all(void)
{
    memset(g_led_state, 0, sizeof(g_led_state));
    for (int i = 0; i < LED_MAX; i++)
    {
        int pin = led_gpio_table[i];
        gpio_export(pin);
        gpio_dir_out(pin);
        gpio_write(pin, 0);
        g_led_state[i] = 0;
    }
    printf("[LED] all led init ok\n");
}

void device_led_deinit_all(void)
{
    for (int i = 0; i < LED_MAX; i++)
    {
        int pin = led_gpio_table[i];
        gpio_write(pin, 0);
        g_led_state[i] = 0;
        gpio_unexport(pin);
    }
    printf("[LED] all led deinit ok\n");
}

void device_led_set(led_idx_t led, uint8_t val)
{
    if (led < 0 || led >= LED_MAX)
        return;

    int pin = led_gpio_table[led];
    gpio_write(pin, val ? 1 : 0);
    g_led_state[led] = val ? 1 : 0; // 同步更新状态缓存
}

// 快捷开关
void device_led_on(led_idx_t led)
{
    device_led_set(led, 1);
}
void device_led_off(led_idx_t led)
{
    device_led_set(led, 0);
}

void device_led_all_on(void)
{
    for (int i = 0; i < LED_MAX; i++)
    {
        device_led_set(i, 1);
    }
}

void device_led_all_off(void)
{
    for (int i = 0; i < LED_MAX; i++)
    {
        device_led_set(i, 0);
    }
}

// 读取单灯缓存状态，无需读硬件文件
uint8_t device_led_get_state(led_idx_t led)
{
    if (led < 0 || led >= LED_MAX)
        return 0;
    return g_led_state[led];
}

// 批量导出所有灯状态
void device_led_get_all_state(uint8_t out_buf[LED_MAX])
{
    memcpy(out_buf, g_led_state, sizeof(g_led_state));
}

#endif