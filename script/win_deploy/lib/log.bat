@echo off
rem ==========================================================================
rem lib/log.bat — 双输出日志工具（控制台 + 结果文件，UTF-8）
rem
rem 前置：
rem   - run.bat 已 setlocal EnableDelayedExpansion
rem   - 控制台已 chcp 65001
rem
rem 调用方式：
rem   call lib\log.bat init "<结果文件完整路径>"
rem       初始化结果文件（写入 UTF-8 BOM，记录起始信息）
rem   call lib\log.bat log "[INFO] 消息内容"
rem       同时输出到控制台与结果文件
rem   call lib\log.bat file "仅写入文件的行"
rem       只写文件不打印
rem   call lib\log.bat bare "仅控制台不写文件"
rem
rem 设计：
rem   - 一次性写入 BOM，确保 Windows 记事本正确识别中文
rem   - 每次 :log 同时 echo 控制台与 append 文件
rem   - 不在 lib 内 setlocal，使 RESULT_FILE 跨调用持久（由 run.bat 管理 setlocal）
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\log.bat: 未知函数 '%~1' 1>&2
exit /b 99

rem ===================== 函数: init =====================
rem 参数: %2=结果文件路径
rem 副作用：设置全局 RESULT_FILE，写入 UTF-8 BOM
:init
set "RESULT_FILE=%~2"
set "RESULT_DIR="
for %%I in ("!RESULT_FILE!") do set "RESULT_DIR=%%~dpI"
if not exist "!RESULT_DIR!" mkdir "!RESULT_DIR!" >nul 2>&1
rem 写入 UTF-8 BOM (0xEF 0xBB 0xBF)，确保记事本识别中文
powershell -NoProfile -Command "[System.IO.File]::WriteAllBytes('!RESULT_FILE!', [byte[]](0xEF,0xBB,0xBF))" >nul 2>&1
exit /b 0

rem ===================== 函数: log =====================
rem 参数: %2=消息（双输出）
:log
echo %~2
if defined RESULT_FILE echo %~2>>"!RESULT_FILE!"
exit /b 0

rem ===================== 函数: file =====================
rem 参数: %2=消息（仅文件）
:file
if defined RESULT_FILE echo %~2>>"!RESULT_FILE!"
exit /b 0

rem ===================== 函数: bare =====================
rem 参数: %2=消息（仅控制台）
:bare
echo %~2
exit /b 0

rem ===================== 函数: separator =====================
rem 写一条分隔线到双输出
:separator
echo ======== %~2 ========
if defined RESULT_FILE echo ======== %~2 ========>>"!RESULT_FILE!"
exit /b 0
