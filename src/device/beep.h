#ifndef __BEEP_H__
#define __BEEP_H__

/**
 * @brief 初始化蜂鸣器设备
 */
void device_beep_init(void);
/**
 * @brief 取消初始化蜂鸣器设备
 */
void device_beep_deinit(void);

/**
 * @brief 打开蜂鸣器
 */ 
void device_beep_on(void);
/**
 * @brief 关闭蜂鸣器
 */
void device_beep_off(void);


#endif