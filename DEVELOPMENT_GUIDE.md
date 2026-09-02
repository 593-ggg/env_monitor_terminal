# 环境监测终端（RK3568）开发文档

> **版本**：v1.0 | **日期**：2026-08-21
> **适用范围**：`src/`（终端主程序）与 `tcp_client/`（命令行 TCP 客户端）全部源码
> **配套文档**：`tcp.md`（自定义 TCP 通信协议规范，与本文档第 15 章配套阅读）

---

## 目录

- [1 项目概述](#1-项目概述)
- [2 目录结构与模块总览](#2-目录结构与模块总览)
- [3 系统架构总览](#3-系统架构总览)
- [4 启动流程（main → UI 主循环）](#4-启动流程main--ui-主循环)
- [5 配置管理模块 config_manager](#5-配置管理模块-config_manager)
- [6 UI 模块 ui](#6-ui-模块-ui)
- [7 线程模块 thread](#7-线程模块-thread)
- [8 摄像头模块 camera](#8-摄像头模块-camera)
- [9 AI 推理模块 ai](#9-ai-推理模块-ai)
- [10 图像处理模块 image](#10-图像处理模块-image)
- [11 传感器模块 sensor](#11-传感器模块-sensor)
- [12 网络模块 network（TCP 服务端）](#12-网络模块-networktcp-服务端)
- [13 TCP 客户端 tcp_client](#13-tcp-客户端-tcp_client)
- [14 设备控制模块 device](#14-设备控制模块-device)
- [15 通信协议摘要](#15-通信协议摘要)
- [16 构建与部署](#16-构建与部署)
- [17 线程模型与并发安全](#17-线程模型与并发安全)
- [18 资源管理与系统清理](#18-资源管理与系统清理)
- [19 已知问题与开发注意事项](#19-已知问题与开发注意事项)

---

## 1 项目概述

本项目是一个运行于 **RK3568 嵌入式 Linux**（aarch64）的**环境监测终端**，集成了：

| 能力 | 说明 |
| --- | --- |
| 图形界面 | LVGL 9.x 框架，Framebuffer（/dev/fb0）显示 + evdev 触摸输入（1024×600） |
| 视频采集 | V4L2 + mmap，640×480 YUYV → ARGB8888 软件转换，双缓冲 |
| AI 推理 | RKNPU2 驱动 YOLOv8（INT8 量化 .rknn 模型），实时目标检测画框 |
| 环境监测 | GY39 多合一传感器（光照/温湿度/气压/海拔），UART4 串口通信 |
| 网络服务 | 自研二进制 TCP 协议服务器（聊天/私聊/文件传输/远程设备控制/远程 AI 推理） |
| 硬件控制 | 4 路 GPIO LED + 1 路蜂鸣器（/sys/class/gpio 用户态操作） |
| 配套客户端 | `tcp_client` 命令行程序，用于调试/远程操作终端 |

**技术栈**：C99 / C++17 混合、LVGL 9、V4L2、RKNN（RKNPU2）、RGA、POSIX Threads、BSD Socket。

---

## 2 目录结构与模块总览

```
env_monitor_terminal/
├── CMakeLists.txt          # 顶层 CMake（交叉编译 aarch64）
├── build.sh / build.ps1    # 构建脚本
├── tcp.md                  # TCP 通信协议规范文档（615 行）
├── 3rdparty/               # 第三方运行时库（RKNN 头文件/库、RGA、STB、JPEG）
├── utils/                  # Rockchip 工具库（image_utils / file_utils / image_drawing）
├── lvgl/                   # LVGL 9 源码树（含 examples/demos/thorvg）
├── model/                  # yolov8.rknn 模型 + coco_80_labels_list.txt 标签
├── bin/                    # 编译产物输出目录（env_monitor_terminal / tcp_client）
├── src/                    # ★ 终端主程序源码（本开发文档重点）
└── tcp_client/             # ★ 命令行 TCP 客户端源码
```

### 2.1 `src/` 子模块划分

| 目录 | 文件 | 职责 | 依赖 |
| --- | --- | --- | --- |
| 根目录 | `main.c` | 程序入口：LVGL 初始化、硬件初始化、UI 创建、主循环 | 全部 |
| 根目录 | `config.h` | 全局颜色/字体宏（LVGL 风格常量） | lvgl |
| 根目录 | `config_manager.{c,h}` | 配置文件读写、全局配置单例 | 无 |
| `ai/` | `yolov8.{h,cpp}` | RKNN 模型加载/推理（**移植官方 rknn_model_zoo**，裁剪至 RK3568 INT8） | rknn_api, utils |
| `ai/` | `postprocess.{h,cpp}` | YOLOv8 后处理（**移植官方**：INT8 反量化 + NMS + letterbox 还原） | rknn_api |
| `ai/` | `yolov8_wrap.{h,cpp}` | C 接口封装层（**本项目新增**，供 C 代码调用） | ai/* |
| `camera/` | `v4l2_camera.{c,h}` | V4L2 采集驱动、YUYV→ARGB8888、双缓冲 | linux/videodev2 |
| `camera/` | `camera_lvgl.{c,h}` | 摄像头画面 → LVGL 控件适配（含 AI 框叠加） | lvgl, thread |
| `device/` | `gpio_util.{c,h}` | 用户态 GPIO 操作（export/direction/value） | /sys/class/gpio |
| `device/` | `led.{c,h}` | 4 路 LED 控制（GPIO 120/121/123/124） | gpio_util |
| `device/` | `beep.{c,h}` | 蜂鸣器控制（GPIO 111） | gpio_util |
| `font/` | `my_font.h` | 字体声明（`font_*` 字阵数据在 font_*.c） | lvgl |
| `font/` | `font_*.c` | ★ 字模点阵数据（54k~144k 行），非业务代码 | - |
| `image/` | `image.{c,h}` | AI 检测框渲染、抓拍 BMP、BMP 检测推理 | bmp, yolov8_wrap, camera |
| `image/` | `bmp.{c,h}` | BMP 24/32 位图读写（ARGB8888 互转） | - |
| `image/` | `ima_utils.{c,h}` | ARGB8888 缩放（最近邻缩小/双线性放大） | - |
| `network/` | `tcp_protocol.{c,h}` | 协议库：包头/包体封装、粘包解包、发送队列 | - |
| `network/` | `tcp_server.{c,h}` | TCP 服务端：accept 循环、客户端管理器、文件会话、收发线程 | tcp_protocol |
| `network/` | `tcp_cmd_parser.{c,h}` | 服务端命令路由（LED/BEEP/文件/聊天/YOLOv8 等） | tcp_server, device, image |
| `sensor/` | `serial.{c,h}` | 串口打开/读写/关闭（termios） | - |
| `sensor/` | `GY39.{c,h}` | GY39 传感器协议：指令构造 + 数据帧解析 | serial |
| `thread/` | `thread.h` | 4 个后台线程的统一接口声明 | pthread |
| `thread/` | `gy39_thread.c` | GY39 读取线程（轮询解析 + 数据缓存 + 互斥锁） | sensor |
| `thread/` | `camera_thread.c` | 摄像头采集线程（按配置帧率节流） | camera |
| `thread/` | `ai_thread.c` | AI 推理线程（~5fps 循环推理 + 结果缓存） | camera, ai |
| `thread/` | `tcp_server_thread.c` | TCP 服务线程封装（UI 命令 → 线程启停） | network |
| `ui/` | `ui.h` | 全部界面创建/加载接口声明 + 控件上下文结构体 | lvgl |
| `ui/` | `ui_main.c` | 全局键盘、全部界面创建、配置加载入口 | 全部 |
| `ui/` | `ui_widget.{c,h}` | LVGL 通用控件封装（按钮/标签/弹窗/滑块/列表/软键盘） | lvgl, font |
| `ui/` | `ui_boot_src.c` | 开机动画屏（spinner + 2.5s 自动跳登录） | ui_widget |
| `ui/` | `ui_login_src.c` | 登录屏（密码校验 → 首页） | config_manager |
| `ui/` | `ui_home_scr.c` | 首页（时间/状态卡片/3×2 功能按钮/底部栏） | thread, camera, ai |
| `ui/` | `ui_camera_src.c` | 实时画面界面（摄像头开关/拍照/AI 识别开关/结果列表） | camera_lvgl, image |
| `ui/` | `ui_camera.c` | 摄像头控件上下文（创建/定时器/销毁） | camera_lvgl |
| `ui/` | `ui_ai_src.c` | 离线 AI 识别界面（BMP 浏览/推理/保存） | image, ima_utils, bmp |
| `ui/` | `ui_server_src.c` | 服务器管理界面（启停/在线列表/断开/上传文件管理） | tcp_server |
| `ui/` | `ui_settings_src.c` | 系统设置界面（配置表单/校验/关机重启动画/system_cleanup） | 全部 |
| `ui/` | `ui_divice_scr.c` | 硬件控制界面（LED 切换/流水灯/蜂鸣器长按） | device |
| `ui/` | `ui_env_scr.c` | 环境参数监测界面（5 参数卡片实时刷新） | sensor, thread |
| `tests/` | `camera_test.c`, `test.h` | 摄像头模块单元测试（独立入口，未接入主程序） | camera |

### 2.2 `tcp_client/` 模块

| 文件 | 职责 |
| --- | --- |
| `client_main.c` | 命令行入口：连接参数、help、交互循环 |
| `tcp_client.c` | 客户端核心：连接、收发双线程、文件收发/上传下载状态机 |
| `tcp_cmd_parser.c` | 终端命令 → Packet 解析（与服务端独立实现） |
| `tcp_protocol.{c,h}` | 协议库（与服务端同源，差异：ClientConn 无 IP/端口字段） |
| `client` | ELF 编译产物（非源码） |

### 2.3 代码规模统计

> 统计口径：**有效代码行 = 非空行 − 纯注释行**（含 `#include` 等预处理行；行内尾部注释不计减；`font_*.c` 字模数据自动生成，不计入业务代码）。

**总体规模**

| 范围 | 总行数 | 非空行 | 纯注释行 | 有效代码行 | 文件数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `src/`（排除 4 个字阵文件） | 12,385 | 10,935 | 2,227 | **8,708** | 55 |
| `tcp_client/` | 2,145 | 1,880 | 397 | **1,483** | 7 |
| **合计** | **14,530** | **12,815** | **2,624** | **10,191** | **62** |

**字模数据（自动生成，未计入）**：`font_han_16.c` 58,272 行、`font_heiti_16.c` 54,493 行、`font_heiti_24.c` 88,444 行、`font_yezi_40.c` 143,937 行，合计约 **34.5 万行**。

**src/ 按模块分布（有效代码行）**

| 模块 | 有效行 | 占比 | 主要构成 |
| --- | ---: | ---: | --- |
| ui/（9 个界面 + 控件库 + 主控） | 3,305 | 38.0% | ui_home_scr 463 / ui_ai_src 395 / ui_divice_scr 426 / ui_server_src 700 / ui_settings_src 680 |
| network/（协议 + 服务端 + 命令路由） | 1,742 | 20.0% | tcp_server.c 795 / tcp_cmd_parser.c 622 |
| ai/（RKNN 推理 + 后处理 + 封装） | 746 | 8.6% | postprocess.cpp 348 / yolov8.cpp 201 |
| image/（画框 + BMP + 缩放） | 697 | 8.0% | image.c 244 / bmp.c 251 / ima_utils.c 129 |
| thread/（4 个后台线程） | 416 | 4.8% | gy39_thread 130 / ai_thread 114 |
| camera/（V4L2 + LVGL 适配） | 367 | 4.2% | v4l2_camera.c 275 |
| sensor/（串口 + GY39） | 188 | 2.2% | GY39.c 78 / serial.c 71 |
| device/（GPIO + LED + BEEP） | 188 | 2.2% | led.c 77 / gpio_util.c 47 |
| config/（配置管理） | 226 | 2.6% | config_manager.c 180 |
| 其他（main / tests / font 声明 / config.h） | 154 | 1.8% | camera_test.c 76 |

**各文件明细（有效代码行，降序）**

| 文件 | 总行 | 有效行 | 文件 | 总行 | 有效行 |
| --- | ---: | ---: | --- | --- | ---: |
| network/tcp_server.c | 902 | 795 | sensor/GY39.c | 133 | 78 |
| ui/ui_server_src.c | 888 | 700 | device/led.c | 94 | 77 |
| ui/ui_settings_src.c | 934 | 680 | tests/camera_test.c | 102 | 76 |
| network/tcp_cmd_parser.c | 934 | 622 | thread/camera_thread.c | 97 | 72 |
| ui/ui_home_scr.c | 582 | 463 | thread/tcp_server_thread.c | 93 | 72 |
| ui/ui_divice_scr.c | 498 | 426 | sensor/serial.c | 112 | 71 |
| ui/ui_ai_src.c | 512 | 395 | ui/ui_widget.h | 304 | 69 |
| ai/postprocess.cpp | 425 | 348 | ui/ui_boot_src.c | 90 | 64 |
| ui/ui_widget.c | 436 | 308 | ui/ui.h | 286 | 62 |
| camera/v4l2_camera.c | 359 | 275 | image/image.h | 53 | 16 |
| image/bmp.c | 267 | 251 | tcp_client 侧见下 | | |
| ui/ui_env_scr.c | 329 | 244 | | | |
| image/image.c | 293 | 244 | | | |
| ui/ui_camera_src.c | 286 | 221 | | | |
| ai/yolov8.cpp | 262 | 201 | | | |
| config_manager.c | 228 | 180 | | | |
| network/tcp_protocol.c | 223 | 173 | | | |
| image/ima_utils.c | 164 | 129 | | | |
| ai/yolov8_wrap.cpp | 143 | 115 | | | |
| thread/ai_thread.c | 160 | 114 | | | |
| ui/ui_login_src.c | 155 | 112 | | | |
| ui/ui_gy39.c | 133 | 98 | | | |
| ui/ui_camera.c | 118 | 87 | | | |
| sensor/GY39.c | 133 | 78 | | | |

**tcp_client/ 明细**

| 文件 | 总行 | 有效行 |
| --- | ---: | ---: |
| tcp_client.c | 962 | 713 |
| tcp_cmd_parser.c | 578 | 385 |
| tcp_protocol.c | 222 | 173 |
| client_main.c | 95 | 83 |
| tcp_protocol.h | 201 | 90 |
| tcp_cmd_parser.h | 40 | 23 |
| tcp_client.h | 47 | 16 |

**规模画像**：有效代码约 **1 万行**（C 为主，`ai/` 为 C++ 746 行）。UI 层（3.3k 行）与网络层（1.7k 行）是代码重心；核心业务逻辑集中在 `tcp_server.c`、`ui_server_src.c`、`ui_settings_src.c`、`tcp_cmd_parser.c` 四个"千行级"文件。

---

## 3 系统架构总览

本系统是典型的**嵌入式分层架构**：UI 层（LVGL）→ 业务线程层 → 设备驱动抽象层 → Linux 内核接口。

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          UI 层（LVGL 9，1024×600）                        │
│  开机屏 → 登录屏 → 首页 ─┬─ 实时画面 ─┬─ 离线AI识别                         │
│                          ├─ 环境监测  ├─ 服务器管理                         │
│                          ├─ 硬件控制  └─ 系统设置                          │
│  ui_widget 通用控件库（按钮/弹窗/滑块/软键盘/列表/Spinner）                  │
├─────────────────────────────────────────────────────────────────────────┤
│                        业务线程层（pthread，UI 主线程独立）                  │
│  camera_thread 采集线程 │ ai_thread 推理线程 │ gy39_thread 传感器线程        │
│  tcp_server_thread 服务线程（内部再派生每连接 recv/send 双线程）             │
│  线程间共享数据：互斥锁保护（gy39 数据锁 / AI 结果锁 / 帧缓冲锁 / 发送队列锁）  │
├─────────────────────────────────────────────────────────────────────────┤
│                        驱动抽象层（用户态直接操作内核接口）                   │
│  v4l2_camera（V4L2+mmap）│ serial+GY39（termios）│ gpio_util（sysfs GPIO）  │
│  yolov8_wrap（RKNPU2）  │ bmp/image（文件 I/O） │ tcp_protocol（socket）    │
├─────────────────────────────────────────────────────────────────────────┤
│                        Linux 内核 / 硬件（RK3568 SoC）                     │
│  /dev/video9 │ /dev/ttyS4 │ /sys/class/gpio │ /dev/fb0 │ /dev/input/event6 │
│  NPU（RKNPU2）│ 显示控制器 │ 触摸屏 │ 摄像头 │ GY39 │ LED×4 │ BEEP │ 以太网 │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.1 关键设计决策

| 决策 | 理由 |
| --- | --- |
| 摄像头采集与 UI 渲染解耦（双缓冲 + new_frame 标志） | UI 定时器按 33ms 轮询，采集线程独立阻塞等帧 |
| AI 推理单独线程（~200ms/帧） | 推理耗时高，不能阻塞采集与 UI |
| GY39 线程逐字节读 + 帧头对齐解析 | 串口数据是字节流，需自组帧 |
| TCP 每连接 recv/send 双线程 + 环形发送队列 | recv 线程阻塞读，send 线程异步发，互不干扰 |
| 文件大数据直接 `pkt_send_full` 绕过发送队列 | 避免 256KB 分片撑爆 32 格环形队列 |
| 全局配置单例 `config_get_global()` | 各模块共享，设置页改完保存即全局生效 |

### 3.2 主数据流

```
摄像头 /dev/video9 ──V4L2 mmap──▶ camera_thread 采集
        │ YUYV→ARGB8888 (双缓冲)
        ▼
   camera_lvgl_frame_buf ◀──camera_snapshot── camera_lvgl_update（UI 定时器 33ms）
        │ 叠加 AI 检测框（ai_thread_is_running 时）
        ▼
   LVGL lv_image 控件 ──▶ /dev/fb0 显示

GY39 /dev/ttyS4 ──serial──▶ gy39_thread（逐字节组帧解析）
        │ latest_lux / latest_env（互斥锁）
        ▼
   ui_env_scr / ui_gy39 定时器读取刷新

TCP 客户端 ──socket──▶ tcp_server_thread → accept → 每连接 recv/send 线程
        │ cmd_server_dispatch 路由
        ├─▶ 群聊/私聊广播     ──▶ client_mgr 链表
        ├─▶ 文件上传/下载     ──▶ ./uploads/ 目录
        ├─▶ LED/BEEP 控制     ──▶ device 模块
        └─▶ YOLOv8 远程推理   ──▶ image_bmp_detect_save（后台线程）
```

### 3.3 关键全局状态一览（排查问题入口）

| 全局状态 | 定义位置 | 类型 | 读方 / 写方 |
| --- | --- | --- | --- |
| `g_app_config` | config_manager.c | `AppConfig` 单例 | 全模块读；设置页写 |
| `g_led_state[4]` | device/led.c | uint8_t 数组 | 写：device_led_set；读：get_state |
| `cam_fd` / `frame_buf[2]` / `active_idx` / `new_frame_flag` | camera/v4l2_camera.c | int / 指针 / bool | 写：采集线程；读：UI、AI 线程 |
| `lvgl_frame_buf` / `img_dsc` | camera/camera_lvgl.c | 指针 / lv_image_dsc_t | 写：camera_lvgl_update；读：抓拍 |
| `g_latest_result` | thread/ai_thread.c | `yolov8_result_t` | 写：AI 线程；读：UI 定时器 |
| `latest_lux` / `latest_env` | thread/gy39_thread.c | `Gy39LuxData/EnvData` | 写：GY39 线程；读：UI 定时器 |
| `server_is_running` | thread/tcp_server_thread.c | bool | 写：UI 命令；读：各 UI 刷新 |
| `g_mgr.head / count` | network/tcp_server.c | 链表头 / int | 写：client_mgr_*；读：UI 列表 |
| `g_server_shutting_down` | network/tcp_server.c | bool | server_shutdown 防重入 |
| `g_file_sessions[16]` / `g_upload_sessions[8]` | network/tcp_server.c | 会话数组 | 文件传输中转/落盘 |
| `g_file_send_ctx` / `g_file_recv_ctx` / `g_file_upload_ctx` | tcp_client/tcp_client.c | 文件上下文 | 客户端收发状态机 |
| `kb`（全局软键盘） | ui/ui_main.c | `lv_obj_t*` | 各界面文本框绑定 |

---

## 4 启动流程（main → UI 主循环）

```
main()
 ├─ lv_init()                        // LVGL 核心初始化
 ├─ lv_linux_fbdev_create()          // Framebuffer 显示驱动（/dev/fb0）
 ├─ lv_evdev_create(POINTER, /dev/input/event6)  // 触摸输入
 ├─ ui_widget_style_init()           // 全局按钮统一样式（必须最先调用）
 ├─ device_led_init_all()            // GPIO 导出 120/121/123/124 并清零
 ├─ device_beep_init()               // GPIO 导出 111 并清零
 ├─ ui_main()                        // ★ 创建全部界面 + 加载配置
 └─ while(1){ lv_timer_handler(); usleep(5000); }   // LVGL 主循环 5ms 节拍
```

### 4.1 `ui_main()` 内部流程

```
ui_main()
 ├─ config_load() → 不存在则 config_set_defaults + config_save 创建 system_config.txt
 ├─ ui_widget_create_keyboard(...)   // 全局软键盘（lv_layer_sys，1024×200，默认隐藏）
 ├─ ui_home_scr_create()             // 创建 8 个界面（仅创建不加载）
 │  ...（camera / ai / server / device / settings / env / boot / login）
 ├─ ui_settings_set_config_ptr(cfg)  // 设置页绑定全局配置指针
 └─ ui_boot_scr_load()               // 加载开机屏，2.5s 定时器 → 登录屏
```

界面流转（`ui_src_load` 统一调用 `lv_screen_load`，主界面用 `lv_screen_load_anim` 动画切换）：

```
开机动画(2.5s) → 登录屏(密码校验) → 首页
首页 ──tag1──▶ 实时画面 ──返回──▶ 首页
首页 ──tag2──▶ 离线AI识别
首页 ──tag3──▶ 环境数据
首页 ──tag4──▶ 服务器管理
首页 ──tag5──▶ 硬件控制
首页 ──tag6──▶ 系统设置（保存/重启/退出）
```

---

## 5 配置管理模块 config_manager

### 5.1 配置文件

- 路径：`./system_config.txt`（相对工作目录，`CONFIG_FILE_PATH`）
- 格式：`key=value` 文本，支持 `#` 注释与 `[section]` 分组（分组名仅作注释，解析时忽略）
- 解析规则（`parse_line`）：跳过前导空格/空行/`#`；`=` 分割键值；键值两端去空白；键长 ≤63，值长 ≤255

### 5.2 全局配置结构体 `AppConfig`

```c
typedef struct {
    // TCP 服务器
    int   server_port;               // 监听端口（默认 8888）
    char  server_display_ip[64];     // 界面显示用 IP（不影响实际绑定）
    char  server_upload_dir[256];    // 服务端上传目录（默认 ./uploads/）
    // 摄像头
    int   camera_fps;                // 帧率 5-30（默认 30）
    int   camera_scale;              // 分辨率缩放 10-100%（默认 100）
    char  photo_save_path[256];      // 拍照保存路径（默认 ./photo）
    // AI 推理
    char  ai_model_path[256];        // 模型路径（默认 ./model/yolov8.rknn）
    int   ai_confidence;             // 置信度 0-100%（默认 75）
    // 系统安全
    char  login_password[64];        // 登录密码（默认 123456）
    // 环境监测
    int   env_monitor_enabled;       // 0=关 1=开（默认 0）
} AppConfig;
```

### 5.3 关键接口

| 接口 | 说明 |
| --- | --- |
| `config_set_defaults(cfg)` | 填充默认值 |
| `config_load(path, cfg)` | 加载（传 NULL 用默认路径）；文件不存在返回 -1 且已填默认值；加载后统一做范围钳制 |
| `config_save(path, cfg)` | 写入带分组的配置文件 |
| `config_get_global()` | 返回静态全局单例 `&g_app_config`（线程安全，只读场景无需加锁） |
| `config_get_camera_resolution(scale, &w, &h)` | `scale% × 772×579` 换算实际分辨率 |
| `config_get_ai_confidence(percent)` | 百分比 → 0.0~1.0 浮点阈值 |

> 注意：`config_load` 中 `camera_scale` 仅影响 UI 显示尺寸，**不影响** `v4l2_camera` 实际采集分辨率（采集固定 640×480）。

### 5.4 配置文件实例（system_config.txt）

`config_save` 实际写出的完整格式（`[section]` 分组仅作注释，解析器按 `key=value` 平铺解析，顺序无关）：

```ini
# ============================================
# 系统配置文件 - 自动生成，请勿手动修改
# ============================================

[server]
server_port=8888
server_display_ip=0.0.0.0
server_upload_dir=./uploads/

[camera]
camera_fps=30
camera_scale=100
photo_save_path=./photo

[ai]
ai_model_path=./model/yolov8.rknn
ai_confidence=75

[security]
login_password=123456

[env_monitor]
env_monitor_enabled=0
```

**加载后的范围钳制规则**（`config_load` 末尾统一执行，越界自动回默认值）：

| 字段 | 合法范围 | 越界时回退值 |
| --- | --- | --- |
| server_port | 1 ~ 65535 | 8888 |
| camera_fps | 5 ~ 30 | 30 |
| camera_scale | 10 ~ 100（%） | 100 |
| ai_confidence | 0 ~ 100（%） | 75 |
| login_password | 1 ~ 32 字符 | "123456" |

**解析器行为细节**（`parse_line`）：跳过行首空格/Tab；空行与 `#` 注释直接跳过；`=` 前为 key（≤63 字符，去尾空白）、后为 value（≤255 字符，去尾 `\n\r 空格Tab`）；无 `=` 的行返回 -2 忽略；文件不存在时 `config_load` 返回 -1 但已用默认值填充（调用方据此决定是否创建文件）。

---

## 6 UI 模块 ui

### 6.1 通用控件库 `ui_widget`

所有界面复用，统一视觉风格（圆角 box/按钮、蓝底白字按钮、灰系配色、全局软键盘）。

| 接口 | 功能 |
| --- | --- |
| `ui_widget_style_init()` | 初始化按钮通用样式（圆角 6、白字、pad 5），**必须在创建任何界面前调用一次** |
| `ui_widget_create_box()` | 圆角容器底板（可关闭滚动/设置内边距） |
| `ui_widget_create_dot()` | 状态指示灯圆点 |
| `ui_widget_create_btn()` | 标准按钮（常态/按压双色、tag 区分、clicked 回调） |
| `ui_widget_create_label()/_static()` | 动态/静态文本标签 |
| `ui_widget_create_popup()` | 弹窗（全屏遮罩 `COLOR_MASK_OPA=140` + 主体框，可选关闭按钮） |
| `ui_widget_create_img_file()` | 文件图片控件（LVGL 盘符 `A:/` 前缀） |
| `ui_widget_create_img_buffer()` | 内存图像控件（摄像头帧核心） |
| `ui_widget_create_textarea()` | 文本框（可选绑定软键盘焦点联动） |
| `ui_widget_create_keyboard()` | 软键盘（默认隐藏） |
| `ui_widget_create_spinner()` | Loading 转圈动画 |
| `ui_widget_create_roller()` | 滚动选择器 |
| `ui_widget_create_list()` / `list_add_button()` | 列表及可点击项 |
| `ui_widget_create_slider()` 系列 | 滑块 + 数值标签上下文（`ui_slider_ctx_t`） |

**软键盘联动机制**：文本框获得焦点 `LV_EVENT_FOCUSED` → 显示软键盘并 `lv_keyboard_set_textarea` 绑定；失去焦点 `LV_EVENT_DEFOCUSED` → 隐藏解绑。

### 6.2 各界面功能一览

| 界面 | 文件 | 关键功能 |
| --- | --- | --- |
| 开机屏 | `ui_boot_src.c` | spinner + 系统名；2.5s 一次性定时器 → 登录屏 |
| 登录屏 | `ui_login_src.c` | 密码输入（密码模式、长度限制、占位符）；确认→首页+启动首页定时器；退出→`system_cleanup()+exit` |
| 首页 | `ui_home_scr.c` | 顶部时间栏（1s 定时器）、4 状态卡片（摄像头分辨率/AI 模型/网络 IP:端口/传感器开关，2s 定时器）、3×2 功能大按钮、底部运行时长栏（1s 定时器） |
| 实时画面 | `ui_camera_src.c` | 摄像头控件 + 返回/开关/拍照/AI 识别按钮 + 识别结果框（200ms 定时器显示前 15 个目标）；进入界面默认不开启摄像头 |
| 摄像头控件 | `ui_camera.c` | 4:3 自适应容器；33ms 刷新定时器调 `camera_lvgl_update` |
| 离线 AI | `ui_ai_src.c` | BMP 图库/快照/自定义目录浏览（列表）、加载图等比例缩放居中、AI 识别（原地画框）、保存结果 |
| 服务器管理 | `ui_server_src.c` | 服务器信息（IP/端口/状态）、启动/停止开关、在线客户端列表（每行带断开按钮）、上传文件列表（目录浏览/重命名/删除，弹窗交互）；2s 定时器自动刷新 |
| 系统设置 | `ui_settings_src.c` | 配置表单（端口/IP/上传目录/FPS/分辨率/拍照路径/置信度/密码/环境监测开关）、保存（校验→写全局→写文件）、重置默认、重启/退出（1.5s 关机动画 + `system_cleanup`） |
| 硬件控制 | `ui_divice_scr.c` | 4 路 LED 单击切换、全亮/全灭、流水灯（300ms 定时器）、蜂鸣器长按发声（PRESSING/RELEASED 事件） |
| 环境监测 | `ui_env_scr.c` | 温度/湿度/气压/光照/海拔 5 卡片（500ms 定时器），按监测开关与传感器状态显示灰/绿/橙三态 |

### 6.3 事件回调 tag 约定

按钮通过 `uintptr_t tag` 区分业务，如首页 `tag1~6` 对应 6 个功能入口；摄像头界面 `tag0~3` 对应返回/拍照/AI/开关。硬件控制界面另有 `PRESSING(10)/RELEASED(11)` 事件实现"长按发声"。

---

## 7 线程模块 thread

提供 4 个后台线程的统一生命周期接口，全部采用 **flag 置位 + pthread_join** 的协作式停止模型。

### 7.1 线程总览

| 线程 | 入口函数 | 周期 | 停止方式 | 对外共享数据 |
| --- | --- | --- | --- | --- |
| 摄像头采集 | `camera_thread_entry` | 1/fps（默认 33ms） | `cam_thread_stop` | V4L2 双缓冲（frame_lock） |
| AI 推理 | `ai_thread_entry` | 200ms（5fps） | `ai_thread_stop` | `g_latest_result`（g_result_lock） |
| GY39 读取 | `gy39_thread_entry` | 无固定周期（阻塞读串口） | `gy39_thread_stop` | `latest_lux/latest_env`（gy39_data_lock） |
| TCP 服务 | `server_thread_entry` | 阻塞 accept | 关闭 listen fd 唤醒 | 客户端管理器 |

### 7.2 摄像头线程（camera_thread.c）

- 启动前检查：`camera_is_device_exist()`（打开设备 + QUERYCAP 校验）→ `camera_init()` → 建线程
- 循环：`camera_capture_frame()` 失败则 10ms 重试；成功按 `interval_us = 1000000/fps` 节流
- fps 取自 `config_get_global()->camera_fps`

### 7.3 AI 推理线程（ai_thread.c）

- 启动流程：`yolov8_init(model)` → `malloc(CAM_FRAME_SIZE)` 帧缓冲 → 构造 `image_buffer_t`（format=RGBA8888，指向帧缓冲）→ 建线程
- 循环：等 `camera_is_run()` → `camera_snapshot` 取帧 → `yolov8_detect`（内部 RGA 完成 ARGB→RGB + resize + letterbox）→ 加锁拷贝结果到 `g_latest_result`
- 关键点：`g_img_buf.format = IMAGE_FORMAT_RGBA8888`，但摄像头输出 ARGB8888（字节序 BGRA in memory）。实际字节序为 `{B,G,R,A}`，与 RGBA8888 的 `{R,G,B,A}` 定义冲突——**这是代码中的已知隐患**，推理输入颜色通道可能错位（见 19 章）。

### 7.4 GY39 线程（gy39_thread.c）

- 启动：`init_serial(UART4, 9600)` → `gy39_set_auto_output(AUTO_EN|BME_EN|MAX_EN)` → 建线程
- 循环：`serial_recv(fd, &byte, 1)` 逐字节读取；检测到帧头 `0x5A 0x5A` 重置缓冲起点；`gy39_parse_frame` 尝试解析；成功（返回 1/2）则加锁更新对应缓存并清空缓冲
- 缓冲区 64 字节防溢出，无帧头数据 10ms 轮询间隔

### 7.5 TCP 服务线程（tcp_server_thread.c）

- UI 指令接口：`tcp_server_parse_ui_cmd(TCP_UI_CMD_START/CLOSE)`、`tcp_server_disconnect_client(id)`
- 启动：创建 detached 线程跑 `server_start()`（阻塞 accept），避免阻塞 UI
- 停止：`server_shutdown()` 关闭 listen fd，accept 返回错误退出循环
- 断开客户端：`client_mgr_disconnect(id)` 内部 shutdown+close fd 后 join recv 线程

---

## 8 摄像头模块 camera

### 8.1 v4l2_camera（驱动层）

- 设备：`/dev/video9`；格式：`V4L2_PIX_FMT_YUYV` 640×480；缓冲：3 个 mmap buffer
- 初始化序列：open → S_FMT → REQBUFS → QUERYBUF+mmap+QBUF×3 → STREAMON → malloc 双帧缓冲+YUYV 临时缓冲
- 采集（`camera_capture_frame`）：DQBUF → memcpy 到 yuyv_buf → QBUF → 取非活动缓冲下标 → **锁外** YUYV→ARGB8888 转换 → 加锁切换 active_idx + 置 new_frame_flag
- 双缓冲互斥设计：
  - `frame_lock` 保护 `active_idx`、`new_frame_flag`、缓冲读写索引
  - 转换计算在锁外执行（不阻塞 UI 读帧）
- YUYV→ARGB8888：整数近似公式（359/88/183/454 系数），输出内存字节序 `{B,G,R,0xFF}`，与 LVGL/RGA ARGB8888 一致
- `camera_snapshot`：加锁拷贝当前 active 帧（供 AI 线程/抓拍用）
- `camera_is_device_exist`：open + QUERYCAP 检查 `V4L2_CAP_VIDEO_CAPTURE`

### 8.2 camera_lvgl（LVGL 适配层）

- `camera_lvgl_create`：分配 `lvgl_frame_buf`（CAM_FRAME_SIZE），构造 `lv_image_dsc_t`（ARGB8888/640×480/stride 2560），经 `ui_widget_create_img_buffer` 创建控件
- `camera_lvgl_update`（UI 定时器 33ms 调用）：
  1. `camera_has_new_frame()` 有帧 → `camera_snapshot` 拷入 + `camera_clear_new_frame`
  2. AI 线程运行时 → `ai_thread_get_result` + `yolov8_render_detection_inplace` 原地叠加检测框
  3. `lv_obj_invalidate` 触发重绘
- `camera_lvgl_get_display_buffer`：返回当前显示缓冲（含 AI 框），供抓拍 `image_save_bmp` 使用

---

## 9 AI 推理模块 ai

> **代码来源声明**：`ai/` 目录的算法核心（YOLOv8 模型加载 `yolov8.{h,cpp}`、后处理 `postprocess.{h,cpp}`）
> **移植自 Rockchip 官方 rknn_model_zoo 仓库的 YOLOv8 示例**（Apache-2.0 License），并在移植时做了裁剪：
> 删除 RKNPU1 / RV1106 / FP32 等与本平台无关的分支，仅保留 RK3568（RKNPU2、INT8）路径。
> 本项目新增的集成代码：C 接口封装层 `yolov8_wrap.{h,cpp}`（extern "C"）、
> 与采集线程共享帧缓冲的推理线程、检测结果加锁缓存、UI 原位画框、按需加载/释放模型。

### 9.1 文件职责

| 文件 | 语言 | 来源 | 职责 |
| --- | --- | --- | --- |
| `yolov8_wrap.{h,cpp}` | C++（extern "C"） | **本项目新增** | 面向 C 的封装：`yolov8_init/detect/deinit/is_initialized/cls_name/file_all_exist`；持有全局 `rknn_app_context_t g_app_ctx` |
| `yolov8.{h,cpp}` | C++ | 官方移植（裁剪） | RKNN 模型加载（init_yolov8_model）、推理（inference_yolov8_model）、释放 |
| `postprocess.{h,cpp}` | C++ | 官方移植（裁剪） | YOLOv8 后处理：INT8 反量化、DFL 解码、按类 NMS、letterbox 坐标还原、COCO 标签加载 |

### 9.2 推理流水线（inference_yolov8_model）

```
输入 image_buffer_t（RGB888 或 RGBA8888，经 RGA）
 ├─ 1. convert_image_with_letterbox → 模型尺寸 RGB888（114 灰边填充，记录 scale/x_pad/y_pad）
 ├─ 2. rknn_inputs_set（UINT8 + NHWC）
 ├─ 3. rknn_run（NPU 执行）
 ├─ 4. rknn_outputs_get（量化模型 want_float=0，保持 INT8）
 ├─ 5. post_process（3 个尺度分支，每分支 box+score+可选 score_sum 三个输出）
 │     ├─ process_i8：qnt 快速阈值过滤 → 反量化 → compute_dfl 解码框 → 收集候选
 │     └─ 排序（降序）→ 逐类 NMS（0.45）→ 坐标除 scale 减 pad 还原回原图
 └─ 6. rknn_outputs_release + free 临时缓冲
```

### 9.3 关键常量

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `NMS_THRESH` | 0.45 | NMS IoU 阈值 |
| `BOX_THRESH` | 0.25 | 后处理框置信度（与 UI 展示阈值 `ai_confidence` 不同层级） |
| `OBJ_CLASS_NUM` | 80 | COCO 类别数 |
| `YOLOV8_MAX_DET` | 128 | 对外结果数组上限 |
| `LABEL_NALE_TXT_PATH` | ./model/coco_80_labels_list.txt | 标签文件 |

> 模型路径宏 `YOLOV8_MODEL_PATH = "./model/yolov8.rknn"` 硬编码，`ai_thread_start` 未使用配置中的 `ai_model_path`。

### 9.4 核心数据结构与输出张量

**`rknn_app_context_t`（yolov8.h，推理上下文）**

```c
typedef struct {
    rknn_context rknn_ctx;              // RKNN 上下文句柄（rknn_init 创建，rknn_destroy 释放）
    rknn_input_output_num io_num;       // 输入/输出张量个数（输入 1，输出 3 或 6）
    rknn_tensor_attr* input_attrs;      // 输入张量属性数组（malloc 分配）
    rknn_tensor_attr* output_attrs;     // 输出张量属性数组（malloc 分配）
    int model_channel;                  // 输入通道数（3）
    int model_width;                    // 模型输入宽（如 640）
    int model_height;                   // 模型输入高（如 640）
    bool is_quant;                      // 是否 INT8 量化（RK3568 上通常为 true）
} rknn_app_context_t;
```

**YOLOv8 输出张量布局（RK3568 / RKNPU2 / INT8）**

模型输出共 3 个检测尺度（stride 8/16/32），每个尺度通常输出 2 个张量（box + score），部分导出模型为 3 个（多一个 score_sum 用于快速过滤）。张量维度为 `[1, C, H, W]`：

| 输出 | 维度 C | 含义 |
| --- | --- | --- |
| box | 64（= dfl_len 16 × 4） | 边框 DFL 分布参数 |
| score | 80 | 各类别得分 |
| score_sum（可选） | 1 | 全类别得分和，用于低阈值快速剪枝 |

后处理 `post_process` 的关键推导：`dfl_len = output_attrs[0].dims[1] / 4`；`grid = output_attrs[box_idx].dims[2:3]`；`stride = model_in_h / grid_h`。

**量化反量化公式**（process_i8）：

```c
// INT8 → float：  (qnt - zero_point) * scale
float f = ((float)qnt - (float)zp) * scale;
// float → INT8（阈值换算用）： f / scale + zp，裁剪到 [-128, 127]
```

---

## 10 图像处理模块 image

### 10.1 检测框渲染 `yolov8_render_detection_inplace`

- 输入：ARGB8888 帧缓冲 + 宽高 + 检测结果
- 流程：读取配置置信度阈值（`config_get_ai_confidence`）→ 低于阈值不画 → 按 `cls_id % 8` 取 8 色循环调色板 → `draw_rectangle`（线宽 2）→ `draw_text` 绘制 `类名 置信度`（标签在框上方，越界则框内）
- 底层绘制依赖 `utils/image_drawing` 的 `draw_rectangle/draw_text`

### 10.2 抓拍

| 接口 | 说明 |
| --- | --- |
| `image_save_bmp(filepath, bpp)` | 优先取 `camera_lvgl_get_display_buffer()`（含 AI 框）；无则 `camera_snapshot` 兜底；再 `bmp_save_from_argb8888` 输出 24/32 位 BMP |
| `image_snapshot_bmp(dir, bpp)` | mkdir（EEXIST 容忍）→ `snap_YYYYmmdd_HHMMSS.bmp` 命名 → 调 image_save_bmp |

### 10.3 BMP 读写（bmp.c）

- 写出：24 位每行 4 字节对齐（`(w*3+3)&~3`），32 位无对齐；**BMP 行序从下到上**（逐行从最后一行写起）；头 14 字节 + 信息头 40 字节
- 读取：校验 `bfType=0x4D42`、位深仅支持 24/32、压缩必须为 0；支持负高度（自上而下存储）；统一转 ARGB8888 输出并回传原位深

### 10.4 图像缩放（ima_utils.c）

- 缩小：最近邻（快）；放大：双线性（平滑，RGBA 四通道独立插值）；scale≈1 直接 memcpy
- `ima_calc_scale_size` 保证最小 1×1

### 10.5 离线推理链路

`image_bmp_detect_save(input, output)` → `image_bmp_detect`（读 BMP→ARGB）→ `image_argb_detect`（ARGB→RGB888 → yolov8_detect → 原地画框；**按需初始化/释放模型**，即若 yolov8 未初始化则 init，用毕 deinit）→ 按原图位深保存。该函数即服务端 `CMD_YOLOV8` 的后台处理实现。

---

## 11 传感器模块 sensor

### 11.1 串口（serial.c）

- `init_serial(file, baud)`：open O_RDWR → termios 配置（CLOCAL|CREAD、8N1、关硬件流控）→ 支持 9600/19200/38400/115200 → tcflush → tcsetattr
- `serial_send/serial_recv/serial_close`：薄封装 write/read/close
- 设备宏：UART0/1/3/4（`/dev/ttyS0/S1/S3/S4`）

### 11.2 GY39 协议（GY39.c）

- 指令帧：`0xA5 + cfg + sum`（sum = 前两字节累加低 8 位）
- 查询指令：光照 `0xA5 0x51 0xF6`；环境 `0xA5 0x52 0xF7`
- 数据帧（`gy39_parse_frame`）：
  - 帧头 `0x5A 0x5A` + type + data_len + 数据 + 1 字节校验和
  - 校验：除最后 1 字节外全部累加 == 末字节，否则返回 -1
  - type `0x15`（data_len=4）：光照，4 字节大端整数 /100 = lux
  - type `0x45`（data_len=10）：温度(2B/100=℃)、气压(4B/100=hPa)、湿度(2B/100=%)、海拔(2B=m)

### 11.3 数据帧字节布局明细

**环境数据帧（type = 0x45，共 15 字节：5 头 + 10 数据）**

| 偏移 | 长度 | 字段 | 解析（大端） | 单位 |
| --- | ---: | --- | --- | --- |
| 0 | 1 | 帧头 0x5A | 固定 | - |
| 1 | 1 | 帧头 0x5A | 固定 | - |
| 2 | 1 | type = 0x45 | 环境数据标志 | - |
| 3 | 1 | data_len = 10 | 数据区长度 | - |
| 4~5 | 2 | 温度原值 | `uint16_t / 100.0f` | ℃ |
| 6~9 | 4 | 气压原值 | `uint32_t / 100.0f` | hPa |
| 10~11 | 2 | 湿度原值 | `uint16_t / 100.0f` | %RH |
| 12~13 | 2 | 海拔原值 | `uint16_t`（不除） | m |
| 14 | 1 | 校验和 | 字节 0~13 累加低 8 位 | - |

**光照数据帧（type = 0x15，共 9 字节：5 头 + 4 数据）**

| 偏移 | 长度 | 字段 | 解析（大端） | 单位 |
| --- | ---: | --- | --- | --- |
| 0~3 | 4 | 帧头 + type + data_len | 0x5A 0x5A 0x15 0x04 | - |
| 4~7 | 4 | 光照原值 | `uint32_t / 100.0f` | lux |
| 8 | 1 | 校验和 | 字节 0~7 累加低 8 位 | - |

**控制指令**

| 指令 | 用途 | 说明 |
| --- | --- | --- |
| `0xA5 0x80`（bit7 置 1） | 开启自动上报 | 与 BME(bit1)/MAX(bit0) 组合：`0xA5 0x83 sum` |
| `0xA5 0x51 0xF6` | 查询光照 | sum = 0xA5+0x51 = 0xF6 |
| `0xA5 0x52 0xF7` | 查询环境 | sum = 0xA5+0x52 = 0xF7 |

> 线程实测行为：`gy39_thread` 配置 `GY39_AUTO_EN|GY39_BME_EN|GY39_MAX_EN`（0x83）后，模块按自身周期持续上报；线程逐字节读并靠帧头 `0x5A 0x5A` 对齐，避免字节流粘包/断帧。

---

## 12 网络模块 network（TCP 服务端）

### 12.1 架构

```
server_start()（独立线程，阻塞 accept）
  │ accept → handle_accept
  │   ├─ client_conn_create：malloc ClientConn + 双互斥锁 + 32 格环形发送队列 + 默认 id "fdN" + client_mgr_add
  │   ├─ pthread_create recv_thread（client_recv_thread）
  │   └─ pthread_create send_thread（client_send_thread）
  │ 广播 "[system] xxx online"；下发 "id set ok: fdN"
```

### 12.2 客户端管理器（client_mgr_*）

- 数据结构：单向链表 `g_mgr.head` + 计数 + `pthread_mutex_t mtx`；上限 `MAX_CLIENTS=64`
- 接口：add/remove/set_id（查重）/find_by_id/broadcast（排除 fd）/send_to_id/count/list/get_info_list（线程安全快照，供 UI）/disconnect
- `client_mgr_disconnect(id)`：链表摘除 → shutdown+close fd → join recv 线程（recv 线程退出路径负责 free）

### 12.3 收发线程模型

| 线程 | 职责 |
| --- | --- |
| recv_thread | 阻塞 `pkt_recv_full`；`pkt_header_check` 校验魔数/类型/长度上限；按 `pkt_type` 分发（PKT_MSG_TEXT→cmd_server_dispatch；PKT_HEARTBEAT→日志；PKT_FILE_META/DATA/END→文件处理）；异常断连广播 offline；join send 线程；非关机状态 `client_conn_free` |
| send_thread | 轮询 `client_send_dequeue`（空则 10ms sleep）；`socket_mtx` 保护下 `pkt_send_full`；失败退出 |

### 12.4 文件传输会话

| 会话 | 上限 | 结构 | 用途 |
| --- | --- | --- | --- |
| 文件转发会话 | 16 | sender_fd + target(ClientConn*) | 客户端间 P2P 中转、服务端下发下载 |
| 上传会话 | 8 | uploader_fd + file_fd + filename + received | 客户端→服务端落盘 |

- 客户端间传输：META 建立会话（sender_fd→target），DATA 直接 `socket_mtx` 锁内 `pkt_send_full` 绕过队列转发，END 转发后清理会话
- 服务端下载：`exec_cmd_file_download` 校验路径（防 `..` 遍历 + realpath 二次确认）→ 直接 pkt_send_full 发 META/DATA/END（**不走发送队列，保证顺序**）
- 上传：`exec_cmd_file_upload` 校验 → 递归 mkdir 子目录 → open 文件 → 设上传会话 → 回 "upload ready" → 后续 DATA 直接 write 落盘，END 关闭并回 "upload complete"

### 12.5 关闭流程（server_shutdown）

防重入标记 `g_server_shutting_down` → 关 listen fd 唤醒 accept → 清理上传会话（关文件 fd）→ 清空转发会话 → 逐个连接：置 running=false、shutdown+close、join recv/send 线程、释放队列/锁、链表移除、free → 销毁全局锁。

> **double free 防护**：关机时 recv 线程跳过 `client_conn_free`（由 server_shutdown 统一释放），正常断连时才由 recv 线程释放。

### 12.6 服务端命令路由（tcp_cmd_parser.c）

`cmd_server_dispatch(conn, pkt)`：拷贝 body（≤2048 保证 `\0` 结尾）→ 文件类命令状态机校验（非 IDLE 拒绝）→ 按 `pkt_sub_type` 分发：

| 命令 | 执行函数 | 行为 |
| --- | --- | --- |
| CMD_SET_ID | `exec_cmd_set_id` | 注册/改 ID，占用则回复 "id already in use" |
| CMD_MSG | `exec_cmd_broadcast` | 广播 `[发送者]: 消息`（排除发送者） |
| CMD_MSG_PRIVATE | `exec_cmd_private_msg` | 解析 `@target\|msg` 转发 `[发送者]msg`；目标离线回错误 |
| CMD_CLIENT_LIST | `client_mgr_list_to` | 在线列表文本回传 |
| CMD_FILE_PUT | `exec_cmd_file_put` | 客户端间传输：转发 META + 建立会话 |
| CMD_FILE_ACCEPT | `exec_cmd_file_accept` | 转发接受通知给发送者 |
| CMD_FILE_REJECT | `exec_cmd_file_reject` | 转发拒绝通知给发送者 |
| CMD_FILE_UPLOAD | `exec_cmd_file_upload` | 开启服务端落盘会话 |
| CMD_FILE_DOWNLOAD | `exec_cmd_file_download` | 服务端发文件 |
| CMD_FILE_LIST | `exec_cmd_file_list` | 递归列出 uploads 文件 |
| CMD_YOLOV8 | `exec_cmd_yolov8` | 校验输入存在/输出不存在 → 后台线程 `image_bmp_detect_save` → 完成通知 |
| CMD_LED | `exec_cmd_led` | 解析 `idx\|on/off` → device_led_on/off |
| CMD_BEEP | `exec_cmd_beep` | on/off → device_beep_on/off |

### 12.7 连接上下文 `ClientConn`（tcp_protocol.h，服务端/客户端共用）

```c
typedef struct ClientConn {
    int fd;                         // socket fd
    char id[CLIENT_ID_LEN];         // 客户端标识（默认 "fdN"，可 CMD_SET_ID 修改）
    char client_ip[32];             // 客户端 IP（仅服务端填充；客户端版无此字段）
    uint16_t client_port;           // 客户端端口（仅服务端填充）
    bool running;                   // 线程运行标记
    pthread_t recv_tid;             // 接收线程 ID
    pthread_t send_tid;             // 发送线程 ID
    pthread_mutex_t send_mtx;       // 发送队列互斥锁
    pthread_mutex_t socket_mtx;     // socket 写互斥锁（send 线程与文件直发共用）
    enum ConnState state;           // 业务状态机
    Packet* send_queue;             // 环形发送队列（SEND_QUEUE_CAP=32 格）
    int q_capacity, q_head, q_tail; // 队列容量与读写游标
    struct ClientConn* next;        // 链表指针（服务端管理器使用）
} ClientConn;
```

### 12.8 连接状态机（`ConnState` 枚举，服务端/客户端共用）

| 状态 | 值 | 含义 | 允许进入的命令 |
| --- | ---: | --- | --- |
| STATE_IDLE | 0 | 空闲，可执行任意命令 | 所有 |
| STATE_SENDING_FILE | 1 | 正在发送文件（客户端间） | 等待 accept 推送 |
| STATE_RECEIVING_FILE | 2 | 正在接收文件 | 等待 DATA/END |
| STATE_UPLOADING | 3 | 正在上传到服务端 | 等待 "upload ready" |
| STATE_DOWNLOADING | 4 | 正在从服务端下载 | 等待 server META |
| STATE_RELAYING_FILE | 5 | 【仅服务端】转发文件数据 | - |
| STATE_WAITING_FILE_CONFIRM | 6 | 【仅客户端】收到传输请求，等 accept/reject | file_accept / file_reject |

> 服务端与客户端均在 `cmd_server_dispatch` / `client_input_cmd` 入口做状态机校验：**文件类命令（PUT/UPLOAD/DOWNLOAD）仅在 IDLE 允许**；**accept/reject 仅在 WAITING_FILE_CONFIRM 允许**；任何时刻收到 `PKT_ERR` 一律清上下文回到 IDLE。

### 12.9 命令 body 格式汇总（PKT_MSG_TEXT 包体，`|` 分隔）

| CMD | body 格式 | 示例 |
| --- | --- | --- |
| CMD_SET_ID | `id` | `led01` |
| CMD_MSG_PRIVATE | `@target\|msg` | `@led01\|你好` |
| CMD_MSG | `msg` | `大家好` |
| CMD_FILE_PUT | `@target\|filepath\|filesize` | `@led01\|/tmp/a.bin\|1024` |
| CMD_FILE_ACCEPT | `sender_id\|save_path` | `fd5\|./recv/` |
| CMD_FILE_REJECT | `sender_id` | `fd5` |
| CMD_FILE_UPLOAD | `filename\|filesize\|filepath\|server_subdir` | `a.bin\|1024\|/tmp/a.bin\|photo/` |
| CMD_FILE_DOWNLOAD | `filename\|save_path` | `photo/snap.bmp\|./` |
| CMD_YOLOV8 | `input_filename\|output_filename` | `a.bmp\|a_yolov8.bmp` |
| CMD_LED | `idx\|on/off` | `2\|on` |
| CMD_BEEP | `on/off` | `off` |

> META 包（PKT_FILE_META）body 复用同格式：客户端间 `sender_id\|filename\|filesize`；服务端下载时 `server\|pure_name\|filesize`。

### 12.10 文件传输完整时序

**① 客户端间 P2P（经服务端中转）**

```
发送方A                服务端                   接收方B
  |  file @B path        |                         |
  |──CMD_FILE_PUT───────▶|──PKT_FILE_META 转发────▶|  提示"wants to send"
  |                       |                         |──file_accept path──▶|
  |◀──CMD_FILE_ACCEPT────|◀────────────────────────|                      |
  |  (socket_mtx 直发)    |                         |                      |
  |──PKT_FILE_DATA×N────▶|──socket_mtx 锁内转发────▶|──write 落盘────────▶|
  |──PKT_FILE_END───────▶|──转发 END──▶ 清理会话 ──▶|──close──▶ 回 IDLE   |
```

**② 客户端上传到服务端**

```
客户端                          服务端
  |──CMD_FILE_UPLOAD────────────▶|  校验路径→mkdir→open→建立上传会话→STATE_UPLOADING
  |◀──"upload ready" (CMD_FILE_UPLOAD)──|
  |──PKT_FILE_DATA×N（socket_mtx 直发）▶|  server_handle_file_data 按 uploader_fd 查会话→write
  |──PKT_FILE_END────────────────▶|  关闭文件→回 "upload complete"→STATE_IDLE
```

**③ 服务端下发下载**

```
客户端                          服务端
  |──CMD_FILE_DOWNLOAD─────────▶|  校验 realpath 在 uploads 内→
  |◀──PKT_FILE_META(server|name|size)──|  （socket_mtx 直发，不经发送队列，保证顺序）
  |◀──PKT_FILE_DATA×N───────────|
  |◀──PKT_FILE_END──────────────|  恢复 STATE_IDLE
```

> 三路时序共同点：**文件数据一律持 socket_mtx 直发**，绕过环形发送队列（避免 256KB 分片阻塞/占满 32 格队列，且保证 META→DATA→END 顺序）；发送方/服务端同步推进字节数进度提示。

---

## 13 TCP 客户端 tcp_client

### 13.1 运行方式

```bash
./tcp_client [server_ip] [server_port]   # 默认 127.0.0.1:8888
```

### 13.2 命令表

| 命令 | 说明 |
| --- | --- |
| `id xxx` | 设置客户端标识 |
| `msg @user xxx` | 私聊 |
| `msg xxx` | 群聊广播 |
| `heartbeat` | 心跳包 |
| `file @user filepath` | 向用户发文件（对方 file_accept/file_reject） |
| `file_accept [path]` | 接受文件（默认存 ./） |
| `file_reject` | 拒绝文件 |
| `upload filepath [subdir]` | 上传到服务端（可指定子目录） |
| `download filename [path]` | 从服务端下载 |
| `files` | 服务端文件列表 |
| `yolov8 in [out]` | 远程推理（输入须为纯文件名 .bmp，自动生成 `*_yolov8.bmp`） |
| `led idx on\|off` / `beep on\|off` | 远程硬件控制 |
| `whoami` / `list` / `help` / `exit` | 工具命令 |

### 13.3 客户端状态机（ClientConn.state）

```
IDLE ──file @user──▶ SENDING_FILE（等待对方 accept 推送）
IDLE ──收到 META──▶ WAITING_FILE_CONFIRM ──file_accept──▶ RECEIVING_FILE ──END──▶ IDLE
IDLE ──upload──▶ UPLOADING（等 "upload ready" 推送后发数据）
IDLE ──download──▶ DOWNLOADING（等 server META 后建文件）
发送方收到 accept 推送后直接发文件（socket_mtx 保护）──▶ 结束回 IDLE
任何阶段收到 PKT_ERR ──▶ 清理上下文回 IDLE
```

### 13.4 关键实现

- 上下文：`g_file_send_ctx`（发送方）、`g_file_recv_ctx`（接收/下载）、`g_file_upload_ctx`（上传）、`g_file_sender_id`、`g_pending_file_name`
- 接收线程将所有包按推送处理（`handle_push_packet`），文件数据写盘 + 进度百分比显示
- 发送线程与文件直发共用 `socket_mtx`，保证不交错
- 断连处理：recv 失败 → `fclose(stdin)` 解除主线程 `fgets` 阻塞退出
- `client_destroy`：shutdown(SHUT_RD) → close → join 双线程 → 释放队列与锁

---

## 14 设备控制模块 device

### 14.1 GPIO 工具（gpio_util.c）

通过 sysfs 用户态操作 GPIO：`export/unexport`（写 /sys/class/gpio/export，export 后 200ms 延时等待设备节点）、`direction=out`、`value=0/1`。

### 14.2 LED（led.c）

| LED | GPIO | 说明 |
| --- | --- | --- |
| LED_0 | 120 | `device_led_init_all` 统一 export→out→0 |
| LED_1 | 121 | 状态缓存 `g_led_state[]`，`device_led_get_state` 不读硬件 |
| LED_2 | 123 | `device_led_all_on/off` 批量 |
| LED_3 | 124 | |

### 14.3 蜂鸣器（beep.c）

GPIO 111；`device_beep_on/off` 直接写 value；`device_beep_init/deinit` 负责 export/unexport。

---

## 15 通信协议摘要

> 完整规范见 `tcp.md`（615 行，与源码对齐）。以下为速查。

### 15.1 包头（32 字节，pack(1)，网络字节序）

```
magic(4)=0xAA55CCDD | pkt_type(2) | body_len(4) | pkt_sub_type(1) | reserved(8) | reserve(13)
```

### 15.2 包类型 PKT_TYPE

| 值 | 名称 | 含义 |
| --- | --- | --- |
| 1 | PKT_MSG_TEXT | 文本/命令承载包（子类型=CMD_TYPE） |
| 2 | PKT_FILE_META | 文件元信息（发送者\|文件名\|大小） |
| 3 | PKT_FILE_DATA | 文件分片（≤256KB） |
| 4 | PKT_FILE_END | 文件结束 |
| 5 | PKT_HEARTBEAT | 心跳 |
| 6 | PKT_ERR | 错误应答（子类型=ERR_TYPE） |

### 15.3 关键限制

- `MAX_BODY_SIZE` = 512KB（单包体上限）；`FILE_CHUNK_SIZE` = 256KB（分片）
- 发送队列 `SEND_QUEUE_CAP` = 32 格/连接；文件数据直发绕过队列
- 字节序：包头数值一律大端；字符串包体裸 UTF-8 无 `\0`

---

## 16 构建与部署

### 16.1 顶层 CMake 配置

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)    # 交叉编译器
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(TARGET_SOC "rk356x")                        # 决定 3rdparty 库选择
```

输出目录：`bin/`；`add_subdirectory` 依次：3rdparty（RKNN/RGA 库）→ utils（imageutils/fileutils/imagedrawing 静态库）→ lvgl → src → tcp_client。

### 16.2 src 链接依赖

```
env_monitor_terminal:
  lvgl / lvgl::examples / lvgl::demos / lvgl::thorvg
  imageutils / fileutils / imagedrawing（utils 静态库）
  ${LIBRKNNRT}（rknnrt 运行时） ${LIBRGA}（librga）
  m / pthread / dl
```

> 注意：`src/CMakeLists.txt` 用 `file(GLOB ...)` 收集源码，新增 .c/.cpp 文件后需**重新 cmake** 才会纳入构建。

### 16.3 构建命令

```bash
# Linux 交叉编译
mkdir -p build && cd build && cmake .. && make -j$(nproc)
# 或使用脚本
./build.sh        # 查看脚本内容确认工具链前缀
```

### 16.4 运行依赖（目标板）

- 工作目录下需存在：`model/yolov8.rknn`、`model/coco_80_labels_list.txt`、`system_config.txt`（可自动生成）、`./uploads/`（自动创建）、`./photo/`（自动创建）
- 设备节点：`/dev/video9`（摄像头）、`/dev/ttyS4`（GY39）、`/dev/fb0`（显示）、`/dev/input/event6`（触摸，硬件不同需改 main.c）

### 16.5 调试与验证方法

**启动顺序自检**（串口/终端日志按序观察）

```
[Config] loaded: port=8888 fps=30 scale=100 conf=75
[LED] all led init ok
[BEEP] init ok
（开机动画 2.5s → 登录屏）
```

**各子系统功能验证**

| 子系统 | 验证入口 | 预期结果 |
| --- | --- | --- |
| 显示/触摸 | 开机动画出现、可点击"确认"进入首页 | 无花屏、触摸坐标正确 |
| 登录 | 默认密码 `123456` | 错误有红色提示并清空输入框 |
| 摄像头 | 实时画面页点"开启摄像头" | 画面 4:3 显示无变形；点"拍照"后 `./photo/` 生成 BMP |
| AI 实时识别 | 摄像头开启后点"ai识别" | 右侧结果框列出目标类别与置信度；画面叠加检测框 |
| 离线 AI | AI 页选 BMP → 识别 → 保存 | `./image/ai_output.bmp` 带框 |
| GY39 | 设置页开启"环境监测"→ 环境数据页 | 温/湿/压/光/海拔实时刷新，卡片变绿 |
| TCP 服务器 | 服务器页点"启动服务器" | 状态变绿"运行中"，在线数显示 |
| 远程控制 | 终端跑 `./tcp_client` 连板子 | `led 0 on`、`beep on` 实测硬件动作 |
| 远程推理 | 客户端 `upload a.bmp` → `yolov8 a.bmp` | 收到 "processing complete: a_yolov8.bmp" |

**摄像头/AI 通道验证（实测确认字节序方法）**

1. 实时画面点"拍照"，把 `./photo/snap_*.bmp` 拷回 PC 打开
2. 观察红色物体是否显示为红色、绿色物体是否为绿色
3. 若偏色（红↔蓝互换）才说明通道错位，此时再排查 RGA 格式（正常情况下不应发生，见 19 章第 1 条核实结论）

**远程命令速查（tcp_client）**

```bash
./tcp_client 192.168.1.100 8888
id camera01                  # 注册标识
list                         # 查看在线客户端
msg @camera01 hello          # 私聊
upload ./snap.bmp photo/     # 上传到服务端 photo/ 子目录
files                        # 列出服务端文件
download photo/snap.bmp ./   # 下载
yolov8 snap.bmp              # 远程推理（自动生成 snap_yolov8.bmp）
led 0 on                     # 远程点亮 LED0
beep on                      # 远程开蜂鸣器
```

---

## 17 线程模型与并发安全

### 17.1 线程清单

| 线程 | 数量 | 说明 |
| --- | --- | --- |
| main（LVGL 主循环） | 1 | 所有 lv_* 调用必须在此线程（LVGL 非线程安全） |
| camera_thread | 0~1 | 采集（按需启动） |
| ai_thread | 0~1 | 推理 |
| gy39_thread | 0~1 | 传感器 |
| server_thread | 0~1 | accept 循环（detached） |
| 每客户端 recv/send | 0~128 | 双线程/连接 |

### 17.2 共享数据与锁

| 共享数据 | 保护锁 | 访问方 |
| --- | --- | --- |
| V4L2 双缓冲 active_idx/new_frame_flag | `frame_lock`（v4l2_camera.c） | 采集线程写，UI/AI 线程读 |
| `latest_lux/latest_env` | `gy39_data_lock` | gy39 线程写，UI 定时器读 |
| `g_latest_result` | `g_result_lock` | ai 线程写，UI/camera_lvgl 读 |
| 每连接发送队列 | `conn->send_mtx` | 任意入队线程 + send 线程 |
| socket 写 | `conn->socket_mtx` | send 线程 + 文件直发路径 |
| 客户端链表 | `g_mgr.mtx` | 服务端所有 client_mgr_* |
| 文件/上传会话 | `g_file_session_mtx` / `g_upload_session_mtx` | 服务端文件处理 |
| 全局配置 `g_app_config` | 无（视为启动后只读/设置页单线程写） | 全部模块 |

### 17.3 并发注意事项

- **LVGL 非线程安全**：所有 UI 更新必须发生在主循环线程（定时器回调内）；后台线程只更新"数据缓存"，UI 定时器负责搬运到控件
- 文件数据直发需持有 `socket_mtx`，否则与 send 线程并发写同一 fd 会导致包序错乱
- 摄像头双缓冲"锁外转换"设计：转换期间 UI 可能读到半新半旧帧（active_idx 未切），可接受

---

## 18 资源管理与系统清理

### 18.1 system_cleanup()（ui_settings_src.c，统一清理入口）

```
1. ai_thread_stop()（或 yolov8_deinit）→ 释放 RKNN 模型/NPU 句柄
2. cam_thread_stop()（或 camera_deinit）→ STREAMOFF + munmap + close + free
3. camera_lvgl_deinit() → 释放显示适配缓冲
4. gy39_thread_stop() → 关串口 fd
5. tcp_server_parse_ui_cmd(CLOSE) → 关服务器（listen fd + 全部客户端）
6. ui_home_stop_timers() / ui_server_stop_timers()
7. device_led_deinit_all() / device_beep_deinit() → GPIO unexport
8. lv_deinit() → 关 fb0/evdev
```

调用点：登录屏"退出"按钮、设置页"退出程序/重启程序"（关机动画 1.5s 后执行）。

### 18.2 内存所有权约定

| 分配点 | 释放点 | 说明 |
| --- | --- | --- |
| `pkt_recv_full` malloc body | 每循环 `packet_free_body` | 服务端/客户端 recv 线程 |
| `client_conn_create` malloc conn | recv 线程退出路径 `client_conn_free`（正常断连）或 `server_shutdown`（关机） | 注意 double-free 防护 |
| `ai_thread` g_cam_buf | `ai_thread_stop` | |
| `image_bmp_detect` out_buf | 调用方 free | 文档约定 |
| `ui_widget_create_slider` ctx | `ui_widget_slider_delete` | 设置页未调 delete（静态生命周期） |

---

## 19 已知问题与开发注意事项

| # | 问题/注意点 | 影响 | 建议 |
| --- | --- | --- | --- |
| 1 | ✅ **已核实：AI 输入通道并无错位**。摄像头输出内存字节序 [B,G,R,A]（值 0xAARRGGBB，小端），`ai_thread.c` 标为 `IMAGE_FORMAT_RGBA8888` 看似可疑，但 `utils/image_utils.c get_rga_fmt()` 将该枚举映射为 RGA `RK_FORMAT_ARGB_8888`，rga.h 官方注释为 `A:R:G:B 8:8:8:8 little endian`（即内存序 B,G,R,A），与摄像头输出**完全一致**；`image_drawing.c convert_color()` 对 RGBA8888 的注释同样明确 `byte0=B, byte1=G, byte2=R, byte3=A`。命名陷阱：RGA 的 `ARGB_8888` 按"32 位整数值的通道位位置"命名（0xAARRGGBB），而非内存字节序 | 无（仅命名具迷惑性） | 保留现状；若未来直接换用 `RK_FORMAT_RGBA_8888`（内存序 R,G,B,A）才会真正 R/B 交换 |
| 2 | **CPU 回退在 AI 链路不可用**：`convert_image_cpu` 强制 `src->format == dst->format`，AI 链路 src=RGBA8888(4ch) → dst=RGB888(3ch) 必然不等，RGA 一旦失败则推理直接返回 -1，无有效兜底 | RGA 异常时 AI 功能不可用（而非降级） | 为 CPU 路径增加 RGBA→RGB 的通道转换分支 |
| 3 | `ai_thread_start` 硬编码 `YOLOV8_MODEL_PATH`，忽略 `cfg->ai_model_path` | 配置项无效 | 改为从配置读取 |
| 4 | 首页"网络连接"状态卡片为占位实现（`"19    %s:%s"` 字符串有笔误），未接入真实网络检测 | 显示不准确 | 接入真实链路检测 |
| 5 | `v4l2_camera` 实际固定 640×480，`camera_scale` 只影响 UI 容器显示尺寸 | 设置页分辨率修改不改变采集分辨率 | 如需按比例采集需在 S_FMT 阶段换算 |
| 6 | 客户端 `tcp_client.c` 中 `handle_push_set_id` 检查 `blen > 11`，服务端 `id set ok: fdN` 长度恰 11+，边界注意 | 轻微 | 建议改为 ≥ |
| 7 | 服务端 `client_send_enqueue` 中先锁后判空参数（`!conn` 检查在锁内），存在锁前解引用 conn->send_mtx 的空指针风险 | 参数非法时崩溃 | 参数校验移到锁前 |
| 8 | `server_shutdown` 后 `g_mgr.mtx` 被销毁，UI 若再调用 `tcp_server_is_running`/client_mgr_* 会访问已销毁锁 | 重启服务器需重新 `server_start`（会重新 init） | 保持"关闭后不可再操作管理器"约定 |
| 9 | UI 界面大多使用 `lv_obj_clean` + 重建方式刷新列表（服务器界面 2s 一次），高频刷新时开销较大 | 轻微卡顿 | 可改用增量更新 |
| 10 | `exec_cmd_yolov8` 输出文件重名会拒绝（`already exists`） | 需手动改名 | 可加时间戳 |
| 11 | 上传路径校验仅防 `..` 与 `/` 开头，Windows 风格 `\` 未过滤（目标为 Linux，风险低） | 低 | 可补充 |
| 12 | 字体文件 `font_*.c` 为自动生成字模（34 万+行），勿手动编辑 | - | 用 LVGL 字体转换器重新生成 |
| 13 | `tests/` 下摄像头单测独立（直接调 camera_init），未纳入主程序，属于可复用测试入口 | - | 可在调试时替换 main |

---

*本文档基于 `src/` 与 `tcp_client/` 全部源码逐文件整理，如源码变更请同步更新。协议细节以 `tcp.md` 为准。*
