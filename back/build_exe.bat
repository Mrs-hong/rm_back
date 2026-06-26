@echo off
REM -*- coding: utf-8 -*-
REM 会议文档生成器 - 编译为 .exe 脚本 (Windows)
REM
REM 用法: 双击运行或在 cmd 中执行 build_exe.bat

setlocal enabledelayedexpansion

cd /d "%~dp0"

REM -------------------- 配置 --------------------
set APP_NAME=meeting_doc_gen
set ENTRY_FILE=meeting_doc_gen\cli.py
set OUTPUT_DIR=dist
set BUILD_DIR=build

REM -------------------- 检查依赖 --------------------
echo ==> 检查 Python 环境...
python --version
if %errorlevel% neq 0 (
    echo 错误: 未找到 Python，请先安装 Python 3.8+
    pause
    exit /b 1
)

echo ==> 检查 PyInstaller...
python -c "import PyInstaller" 2>nul
if %errorlevel% neq 0 (
    echo PyInstaller 未安装，正在安装...
    pip install pyinstaller
)

echo ==> 安装项目依赖...
pip install -r requirements.txt

REM -------------------- 清理旧构建 --------------------
echo ==> 清理旧构建文件...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
if exist "%APP_NAME%.spec" del /f /q "%APP_NAME%.spec"

REM -------------------- 编译 --------------------
echo ==> 开始编译...

pyinstaller ^
    --onefile ^
    --name "%APP_NAME%" ^
    --clean ^
    --noconfirm ^
    --distpath "%OUTPUT_DIR%" ^
    --workpath "%BUILD_DIR%" ^
    --add-data "meeting_doc_gen;meeting_doc_gen" ^
    --add-data "templates;templates" ^
    "%ENTRY_FILE%"

if %errorlevel% neq 0 (
    echo 编译失败!
    pause
    exit /b 1
)

REM -------------------- 验证 --------------------
set EXE_PATH=%OUTPUT_DIR%\%APP_NAME%.exe

if exist "%EXE_PATH%" (
    echo.
    echo ============================================
    echo   编译成功!
    echo   输出文件: %EXE_PATH%
    echo ============================================
    echo.
    echo 使用方式:
    echo   %APP_NAME%.exe note -c content.html -t "标题" -o output.docx
    echo   %APP_NAME%.exe summary -c content.html -d "{\"theme\":\"会议\"}" -m 2 -o output.docx
    echo   %APP_NAME%.exe package -f file1.docx file2.docx -n "文档包" -o .\output
) else (
    echo 编译失败，未找到输出文件
    pause
    exit /b 1
)

pause
