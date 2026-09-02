#!/bin/bash
# ============================================================
#  env_monitor_terminal 项目一键编译 & 打包脚本
#  用途:
#    1. cmake + make 交叉编译生成 ./bin/env_monitor_terminal
#    2. 拷贝 3rdparty 的 aarch64 动态库到 ./bin/lib/
#    3. 拷贝 model 目录到 ./bin/model/
#    4. 生成 ./bin/run.sh 板端一键执行脚本
#  使用:
#    chmod +x build.sh  (首次)
#    ./build.sh
# ============================================================
set -e

# ---------- 进入脚本所在目录 (即项目根目录) ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "=========================================="
echo "  [1/5] 检查依赖文件是否存在"
echo "=========================================="

RKNN_SO="3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
RGA_SO="3rdparty/librga/Linux/aarch64/librga.so"
MODEL_DIR="model"

MISSING=0
for f in "${RKNN_SO}" "${RGA_SO}"; do
    if [ ! -f "${f}" ]; then
        echo "  ✗ 缺少文件: ${f}"
        MISSING=1
    else
        echo "  ✓ ${f}"
    fi
done
if [ ! -d "${MODEL_DIR}" ]; then
    echo "  ⚠  目录不存在: ${MODEL_DIR} (不会拷贝 model 到 bin/)"
else
    echo "  ✓ ${MODEL_DIR}/"
fi
if [ ${MISSING} -ne 0 ]; then
    echo "错误: 必要的 3rdparty 库缺失，请先补齐再编译。"
    exit 1
fi

echo ""
echo "=========================================="
echo "  [2/5] CMake 配置 + make 编译"
echo "=========================================="

BUILD_DIR="build"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 如果 CMakeCache 不存在，才重新 cmake；否则只 make 增量编译
if [ ! -f "CMakeCache.txt" ]; then
    echo "  -> cmake .."
    cmake ..
else
    echo "  -> 已存在 CMakeCache.txt，跳过 cmake，直接 make"
fi

echo "  -> make -j$(nproc)"
make -j"$(nproc)"

cd "${SCRIPT_DIR}"

# 检查可执行文件是否生成
if [ ! -f "bin/env_monitor_terminal" ]; then
    echo "错误: 编译结束但未找到 bin/env_monitor_terminal，编译可能失败。"
    exit 1
fi
echo "  ✓ bin/env_monitor_terminal 生成成功 ($(du -h bin/env_monitor_terminal | cut -f1))"

echo ""
echo "=========================================="
echo "  [3/5] 拷贝板端运行时库 -> bin/lib/"
echo "=========================================="

mkdir -p "bin/lib"
cp -f "${RKNN_SO}" "bin/lib/librknnrt.so"
cp -f "${RGA_SO}"  "bin/lib/librga.so"
echo "  ✓ bin/lib/librknnrt.so"
echo "  ✓ bin/lib/librga.so"

echo ""
echo "=========================================="
echo "  [4/5] 拷贝 model 目录 -> bin/model/"
echo "=========================================="

if [ -d "${MODEL_DIR}" ]; then
    # 强制同步：先删旧的 model，再把整个目录拷过去，保持最新
    rm -rf "bin/model"
    cp -rf "${MODEL_DIR}" "bin/model"
    FILE_COUNT=$(find "bin/model" -type f | wc -l)
    echo "  ✓ bin/model/ (${FILE_COUNT} 个文件)"
else
    echo "  ⚠  ${MODEL_DIR}/ 不存在，跳过拷贝。"
    echo "       请手动把 yolov8.rknn + coco_80_labels_list.txt 放到 bin/model/"
fi

echo ""
echo "=========================================="
echo "  [5/5] 生成板端一键执行脚本 bin/run.sh"
echo "=========================================="

cat > "bin/run.sh" <<'EOF'
#!/bin/bash
# 板端执行:  LD_LIBRARY_PATH=./lib ./env_monitor_terminal
# 推荐做法: ./run.sh (自动 cd 到脚本所在目录，避免相对路径出错)
set -e

# 切到 run.sh 所在目录，这样 ./lib ./model 相对路径永远正确
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

if [ ! -f "./env_monitor_terminal" ]; then
    echo "错误: 未找到 ./env_monitor_terminal，可执行文件是否在 bin 目录下?"
    exit 1
fi

echo "[RUN] cd ${SCRIPT_DIR}"
echo "[RUN] LD_LIBRARY_PATH=./lib ./env_monitor_terminal $*"
echo "----------------------------------------------------"

export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH}"
exec "${SCRIPT_DIR}/env_monitor_terminal" "$@"
EOF

chmod +x "bin/run.sh"
echo "  ✓ bin/run.sh (已加执行权限)"

echo ""
echo "=========================================="
echo "  ✓ 全部完成！输出目录: ${SCRIPT_DIR}/bin"
echo "=========================================="
echo ""
echo "  拷贝到开发板 (示例, 把 root@rk356x 改成你自己的):"
echo "    scp -r ${SCRIPT_DIR}/bin root@rk356x:/root/eee/"
echo ""
echo "  开发板上直接执行:"
echo "    cd /root/eee/bin"
echo "    ./run.sh"
echo ""
echo "  或者按你原来的方式手动执行:"
echo "    cd /root/eee/bin"
echo "    LD_LIBRARY_PATH=./lib ./env_monitor_terminal"
echo ""
