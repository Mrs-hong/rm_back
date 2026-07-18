@echo off
rem ==========================================================================
rem lib/ini.bat — 简易 INI 配置解析器
rem
rem 依赖环境变量：
rem   INI_FILE  config.ini 的完整路径（由 run.bat 启动时设置）
rem
rem 调用方式：
rem   call lib\ini.bat get <section> <key> <output_var_name>
rem   例: call lib\ini.bat get device ip DEVICE_IP
rem   执行后 %DEVICE_IP% 即为配置值；键不存在则变量被 unset
rem
rem 设计：
rem   - 纯 bat 实现，无 PowerShell 依赖（PS 仅用于 UTF-8 解码与超时）
rem   - 支持节 [section]、键 key=value、行内分号注释、空行
rem   - 不支持嵌套节、不支持的转义（够用即可）
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\ini.bat: 未知函数 '%~1' 1>&2
exit /b 99

rem ===================== 函数: get =====================
rem 参数: %2=section  %3=key  %4=输出变量名
:get
setlocal EnableDelayedExpansion
set "_want_section=[%~2]"
set "_want_key=%~3"
set "_cur_section="
set "_value="
set "_found=0"

if not exist "%INI_FILE%" (
    endlocal & set "%~4="
    exit /b 1
)

for /f "usebackq eol=; tokens=1,* delims==" %%a in ("%INI_FILE%") do (
    set "_raw=%%a"
    rem 节头行：首字符为 '['
    if "!_raw:~0,1!"=="[" (
        set "_cur_section=!_raw!"
    ) else if "!_cur_section!"=="!_want_section!" (
        rem 去除键两端空白（for /f tokens=* 会 trim 前导）
        for /f "tokens=*" %%x in ("!_raw!") do set "_k=%%x"
        if !_found! equ 0 if /i "!_k!"=="!_want_key!" (
            set "_value=%%b"
            set "_found=1"
        )
    )
)
if !_found! equ 1 (
    endlocal & set "%~4=%_value%"
    exit /b 0
) else (
    endlocal & set "%~4="
    exit /b 1
)

rem ===================== 函数: list =====================
rem 参数: %2=section  输出该节所有 key=value 到 stdout（调试用）
:list
setlocal EnableDelayedExpansion
set "_want_section=[%~2]"
set "_cur_section="
for /f "usebackq eol=; tokens=1,* delims==" %%a in ("%INI_FILE%") do (
    set "_raw=%%a"
    if "!_raw:~0,1!"=="[" (
        set "_cur_section=!_raw!"
    ) else if "!_cur_section!"=="!_want_section!" (
        echo %%a=%%b
    )
)
endlocal
exit /b 0
