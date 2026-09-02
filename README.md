# 环境监测终端（RK3568）

基于 **RK3568 嵌入式 Linux** 的多功能环境监测终端：LVGL 图形界面、USB 摄像头实时画面、NPU 加速的 YOLOv8 目标检测、GY39 环境传感器（温湿度/气压/光照/海拔）、自研二进制 TCP 协议（支持聊天、文件传输、远程设备控制、远程 AI 推理）。

## 功能特性

| 模块 | 能力 |
| --- | --- |
| 🖥 图形界面 | LVGL 9，1024×600 触摸屏：开机动画 / 登录 / 首页 / 实时画面 / 离线 AI / 环境监测 / 服务器管理 / 硬件控制 / 系统设置 |
| 📷 实时画面 | V4L2 采集 640×480 YUYV → ARGB8888 软件转换，双缓冲显示；一键拍照存 BMP |
| 🤖 AI 识别 | 移植 RKNPU2 官方 YOLOv8 推理流程（INT8 量化 .rknn）并深度集成：实时画面叠加检测框；离线图片推理与结果保存 |
| 🌡 环境监测 | GY39 多合一传感器（UART4），实时显示温度/湿度/气压/光照/海拔 |
| 🌐 网络服务 | 自研二进制 TCP 服务器（`:8888`）：群聊/私聊、客户端间文件传输、上传下载、远程 LED/蜂鸣器控制、远程 AI 推理 |
| 💡 硬件控制 | 4 路 GPIO LED（开关/流水灯）+ 蜂鸣器（长按发声） |
| ⌨️ 配套客户端 | 命令行 `tcp_client`，远程调试与控制的入口 |

## 硬件与环境要求

- **SoC**：RK3568（aarch64），RKNPU2 + RGA
- **设备节点**（默认，可按实际硬件修改源码）：

| 设备 | 节点 | 用途 |
| --- | --- | --- |
| 摄像头 | `/dev/video9` | V4L2 视频采集 |
| 传感器 | `/dev/ttyS4` | GY39 串口（9600 8N1） |
| 显示 | `/dev/fb0` | LVGL Framebuffer |
| 触摸 | `/dev/input/event6` | 触摸输入（`src/main.c` 中可改） |
| GPIO | `/sys/class/gpio` | LED 120/121/123/124，蜂鸣器 111 |

- **交叉编译工具链**：`aarch64-linux-gnu-gcc/g++`（v7.5+）

## 目录结构

```
env_monitor_terminal/
├── CMakeLists.txt          # 顶层构建（交叉编译）
├── build.sh / build.ps1    # 构建脚本
├── src/                    # ★ 终端主程序源码（分层：ui / thread / camera / ai / network / sensor / device / image）
├── tcp_client/             # ★ 命令行 TCP 客户端源码
├── utils/                  # Rockchip 工具库（image_utils / file_utils / image_drawing）
├── 3rdparty/               # 第三方运行时库（RKNN / RGA / STB / JPEG）
├── lvgl/                   # LVGL 9 源码树
├── model/                  # yolov8.rknn + coco_80_labels_list.txt
├── bin/                    # 编译输出（env_monitor_terminal / tcp_client）
├── tcp.md                  # TCP 通信协议规范（615 行）
└── DEVELOPMENT_GUIDE.md    # ★ 深度开发文档（19 章，含代码规模统计）
```

## 构建

```bash
# 1. 确保交叉编译工具链在 PATH 中
export PATH=$PATH:/path/to/gcc-linaro-7.5.0-aarch64/bin

# 2. 配置 + 编译（Linux）
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 3. 产物输出到 bin/
ls ../bin/    # env_monitor_terminal  tcp_client
```

> Windows 下可参考 `build.ps1`；`src/CMakeLists.txt` 使用 `file(GLOB ...)` 收集源码，**新增源文件后需重新执行 cmake**。

## 部署与运行（目标板）

```bash
# 1. 拷贝可执行文件与运行依赖到板子（保持相对目录结构）
scp bin/env_monitor_terminal root@<ip>:/opt/env_monitor/
scp -r model root@<ip>:/opt/env_monitor/

# 2. 在板子上启动（需在程序工作目录下）
cd /opt/env_monitor
./env_monitor_terminal
```

首次启动自动生成 `system_config.txt`（默认配置）；`./uploads/`、`./photo/` 目录在使用时自动创建。

## 使用说明

### 界面操作流程

```
开机动画(2.5s) → 登录（默认密码 123456）→ 首页
首页 → 实时画面（开启摄像头 → 拍照 / AI 识别）
首页 → 离线 AI（选图 → 识别 → 保存）
首页 → 环境数据（需先在"系统设置"开启环境监测）
首页 → 服务器管理（启动服务器 → 查看/断开在线客户端 → 管理上传文件）
首页 → 硬件控制（LED 切换 / 流水灯 / 蜂鸣器长按）
首页 → 系统设置（端口 / 帧率 / 分辨率 / 置信度 / 密码 / 环境监测开关 / 重启 / 退出）
```

### 远程控制（tcp_client）

```bash
# 连接终端（默认 127.0.0.1:8888）
./tcp_client <终端IP> <端口>

id camera01                  # 注册客户端标识
list                         # 查看在线客户端
msg @camera01 hello          # 私聊
msg 大家好                    # 群聊广播
file @camera01 ./a.bin       # 向客户端传文件（对方 file_accept / file_reject）
upload ./snap.bmp photo/     # 上传到服务端 photo/ 子目录
download photo/snap.bmp ./   # 从服务端下载
files                        # 列出服务端文件
yolov8 snap.bmp              # 远程 AI 推理（自动生成 snap_yolov8.bmp）
led 0 on                     # 远程点亮 LED0
beep on                      # 远程开启蜂鸣器
exit
```

## 配置文件

`system_config.txt`（程序工作目录下，设置页修改后自动保存）：

```ini
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

> 密码长度 1~32；`camera_scale` 仅影响界面显示尺寸，采集分辨率固定 640×480。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| `DEVELOPMENT_GUIDE.md` | 深度开发文档：19 章模块解析、协议、线程模型、资源管理、代码规模统计（有效代码约 1 万行）、12 条已知问题 |
| `tcp.md` | 自定义 TCP 协议完整规范（包头格式、命令枚举、文件传输协议） |
| `architecture.html` | 交互式项目架构图（分层架构 / 数据流 / 线程模型 / 界面流转） |
| `INTERVIEW_GUIDE.md` | 面试项目手册：简历项目写法、自我介绍脚本、高频问题应答、难点 STAR、深挖追问 |

## 已知问题速览

详见 `DEVELOPMENT_GUIDE.md` 第 19 章，重点：

1. AI 输入通道**无错位**（已核实，RGA 格式映射自洽）
2. CPU 回退在 AI 链路不可用（RGA 失败时推理直接报错）
3. `ai_model_path` 配置项未生效（模型路径硬编码）
4. `camera_scale` 不影响实际采集分辨率

---

**技术栈**：C99 / C++17 · LVGL 9 · V4L2 · RKNPU2(RKNN) · RGA · POSIX Threads · BSD Socket
