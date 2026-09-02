#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "lvgl/lvgl.h"
#include "my_font.h"

// 大标题用字（24号黑体，最大的字）
#define FONT_TITLE          &font_heiti_24
// 小标题用字（16号黑体，次级标题）
#define FONT_SUBTITLE       &font_heiti_16
// 正文用字（16号宋体，普通文字）
#define FONT_BODY           &font_han_16
// 棋盘方块内的数字用字
#define FONT_BLOCK          &font_han_16

// ========== 中性灰色系 ==========
#define COLOR_GRAY_LIGHTEST    lv_color_hex(0xF5F5F5)    // 极浅灰，背景底色
#define COLOR_GRAY_LIGHT       lv_color_hex(0xE8E4DD)    // 浅灰，空白方块底色
#define COLOR_GRAY_MID         lv_color_hex(0xD3CDBE)    // 中浅灰，次要背景
#define COLOR_GRAY_DARK        lv_color_hex(0x9C9482)    // 深灰，弱化文字、辅助线条
#define COLOR_GRAY_BLACKISH    lv_color_hex(0x3A3830)    // 近黑灰，深色文字

// ========== 黄色 / 橙色系 ==========
#define COLOR_YELLOW_LIGHTEST  lv_color_hex(0xFFF6E0)    // 极浅黄
#define COLOR_YELLOW_LIGHT     lv_color_hex(0xFFE8B3)    // 浅黄
#define COLOR_YELLOW_NORMAL    lv_color_hex(0xFFD966)    // 标准黄色
#define COLOR_YELLOW_ORANGE    lv_color_hex(0xFFC247)    // 黄橙色
#define COLOR_ORANGE_LIGHT     lv_color_hex(0xFFA840)    // 浅橙色
#define COLOR_ORANGE_NORMAL    lv_color_hex(0xFF8C33)    // 标准橙色
#define COLOR_ORANGE_DARK      lv_color_hex(0xFF6F22)    // 深橙色

// ========== 红色系 ==========
#define COLOR_RED_LIGHT        lv_color_hex(0xFF7050)    // 浅红色
#define COLOR_RED_NORMAL       lv_color_hex(0xF54831)    // 标准红色
#define COLOR_RED_DARK         lv_color_hex(0xE03020)    // 深红色

// ========== 紫色系 ==========
#define COLOR_PURPLE_LIGHT     lv_color_hex(0xE8A2EA)    // 浅紫色
#define COLOR_PURPLE_NORMAL    lv_color_hex(0xD160D6)    // 标准紫色
#define COLOR_PURPLE_DARK      lv_color_hex(0xA834AD)    // 深紫色

// ========== 蓝色系 ==========
#define COLOR_BLUE_LIGHTEST    lv_color_hex(0xD6ECFF)    // 极浅蓝
#define COLOR_BLUE_LIGHT       lv_color_hex(0x82C1FF)    // 浅蓝色
#define COLOR_BLUE_NORMAL      lv_color_hex(0x3488E3)    // 标准蓝色
#define COLOR_BLUE_DARK        lv_color_hex(0x205FA8)    // 深蓝色

// ========== 绿色系 ==========
#define COLOR_GREEN_LIGHT      lv_color_hex(0xB8E899)    // 浅绿色
#define COLOR_GREEN_NORMAL     lv_color_hex(0x66C966)    // 标准绿色
#define COLOR_GREEN_DARK       lv_color_hex(0x2E9E48)    // 深绿色

// ========== 文字色系 ==========
#define COLOR_TEXT_DARK        lv_color_hex(0x423E36)    // 深色文字
#define COLOR_TEXT_LIGHT       lv_color_hex(0xFAF8F2)    // 浅色文字（米白）
#define COLOR_TEXT_WHITE       lv_color_hex(0xFFFFFF)    // 纯白色文字
#define COLOR_TEXT_BLACK       lv_color_hex(0x000000)    // 纯黑色文字

// ========== 边框通用色 ==========
#define COLOR_BORDER           lv_color_hex(0xC8C2B0)    // 控件边框、分割线

#endif
