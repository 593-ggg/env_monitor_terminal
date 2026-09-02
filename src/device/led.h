#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>

// LED编号枚举
typedef enum {
    LED_0 = 0,
    LED_1 = 1,
    LED_2 = 2,
    LED_3 = 3,
    LED_MAX
} led_idx_t;

// 初始化全部四路LED
void device_led_init_all(void);

// 反初始化全部四路LED
void device_led_deinit_all(void);

// 设置指定LED电平，并同步记录状态
void device_led_set(led_idx_t led, uint8_t val);

// 快捷开关
void device_led_on(led_idx_t led);

void device_led_off(led_idx_t led);


// 批量控制
void device_led_all_on(void);
void device_led_all_off(void);

// 获取指定LED当前状态 1亮 / 0灭
uint8_t device_led_get_state(led_idx_t led);

// 获取全部LED状态数组
void device_led_get_all_state(uint8_t out_buf[LED_MAX]);

#endif