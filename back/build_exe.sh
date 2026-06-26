#!/bin/bash
# -*- coding: utf-8 -*-
# 会议文档生成器 - 编译为 .exe 脚本 (Linux/macOS)
#
# 用法:
#   chmod +x build_exe.sh
#   ./build_exe.sh
#
# 输出: dist/meeting_doc_gen (Linux) 或 dist/meeting_doc_gen.exe (交叉编译)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# -------------------- 配置 --------------------
APP_NAME="meeting_doc_gen"
ENTRY_FILE="meeting_doc_gen/cli.py"
OUTPUT_DIR="dist"
BUILD_DIR="build"
SPEC_FILE="${APP_NAME}.spec"

# -------------------- 检查依赖 --------------------
echo "==> 检查 Python 环境..."
python3 --version

echo "==> 检查 PyInstaller..."
if ! python3 -c "import PyInstaller" 2>/dev/null; then
    echo "PyInstaller 未安装，正在安装..."
    pip install pyinstaller
fi

echo "==> 安装项目依赖..."
pip install -r requirements.txt

# -------------------- 清理旧构建 --------------------
echo "==> 清理旧构建文件..."
rm -rf "$BUILD_DIR" "$OUTPUT_DIR" "$SPEC_FILE"

# -------------------- 编译 --------------------
echo "==> 开始编译..."

# 判断当前平台
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macos"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    PLATFORM="windows"
else
    PLATFORM="unknown"
fi

echo "  当前平台: $PLATFORM"

# 构建 PyInstaller 命令
PYINSTALLER_CMD=(
    pyinstaller
    --onefile                          # 打包为单文件
    --name "$APP_NAME"                 # 输出文件名
    --clean                            # 清理临时文件
    --noconfirm                        # 覆盖输出目录
    --distpath "$OUTPUT_DIR"           # 输出目录
    --workpath "$BUILD_DIR"            # 临时构建目录
    --add-data "meeting_doc_gen:meeting_doc_gen"  # 包含源码包
    --add-data "templates:templates"   # 包含模板目录
)

# 隐藏控制台窗口 (仅 Windows)
if [[ "$PLATFORM" == "windows" ]]; then
    PYINSTALLER_CMD+=(--windowed)
fi

# 添加入口文件
PYINSTALLER_CMD+=("$ENTRY_FILE")

echo "  执行命令: ${PYINSTALLER_CMD[*]}"
"${PYINSTALLER_CMD[@]}"

# -------------------- 验证 --------------------
if [[ "$PLATFORM" == "windows" ]]; then
    EXE_PATH="$OUTPUT_DIR/${APP_NAME}.exe"
else
    EXE_PATH="$OUTPUT_DIR/$APP_NAME"
fi

if [ -f "$EXE_PATH" ]; then
    FILE_SIZE=$(du -h "$EXE_PATH" | cut -f1)
    echo ""
    echo "============================================"
    echo "  编译成功!"
    echo "  输出文件: $EXE_PATH"
    echo "  文件大小: $FILE_SIZE"
    echo "============================================"
    echo ""
    echo "使用方式:"
    echo "  $EXE_PATH note -c content.html -t '标题' -o output.docx"
    echo "  $EXE_PATH summary -c content.html -d '{\"theme\":\"会议\"}' -m 2 -o output.docx"
    echo "  $EXE_PATH package -f file1.docx file2.docx -n '文档包' -o ./output"
else
    echo "编译失败，未找到输出文件"
    exit 1
fi
