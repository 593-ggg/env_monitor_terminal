#ifndef __GPIO_UTIL_H__
#define __GPIO_UTIL_H__

#include <stdint.h>

/**
 * @brief 导出gpio引脚 /sys/class/gpio/export
 * @param pin gpio编号
 */
void gpio_export(int pin);

/**
 * @brief 取消导出gpio引脚 /sys/class/gpio/unexport
 * @param pin gpio编号
 */
void gpio_unexport(int pin);

/**
 * @brief 设置gpio为输出方向
 * @param pin gpio编号
 */
void gpio_dir_out(int pin);

/**
 * @brief 输出电平到gpio
 * @param pin gpio编号
 * @param val 0低电平 / 1高电平
 */
void gpio_write(int pin, uint8_t val);

#endif