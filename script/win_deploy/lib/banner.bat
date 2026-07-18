@echo off
rem ==========================================================================
rem lib/banner.bat — 醒目的 PASS/FAIL 横幅 + 蜂鸣
rem
rem 设计：
rem   - 用 `color` 全屏变色（Win7+ 全兼容，不依赖 ANSI 支持）
rem   - 用 PowerShell [console]::beep 蜂鸣（跨 Win7/10/11 一致）
rem   - 大字 ASCII 横幅便于产线远距离辨识
rem   - 显示后恢复默认颜色 color 07
rem
rem 调用方式：
rem   call lib\banner.bat pass "<副标题>"
rem   call lib\banner.bat fail "<副标题>"
rem   call lib\banner.bat info "<副标题>"   （中性蓝色横幅）
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\banner.bat: 未知函数 '%~1' 1>&2
exit /b 99

:pass
call :render 0a "  PPPP    AAA   SSSS  SSSS " "%~2"
goto :eof

:fail
call :render 0c "  FFFF   AAA   III  LLLL " "%~2"
goto :eof

:info
call :render 09 "  INFO " "%~2"
goto :eof

rem ===================== 内部: render =====================
rem 参数: %2=color(hex)  %3=横幅文本  %4=副标题
:render
setlocal EnableDelayedExpansion
set "_color=%~2"
set "_banner=%~3"
set "_subtitle=%~4"
color !_color!
echo.
echo ============================================================
echo !_banner!
echo ============================================================
if not "!_subtitle!"=="" echo !_subtitle!
echo ============================================================
rem 蜂鸣（800Hz, 300ms）— 跨 Win7/10/11 一致
powershell -NoProfile -Command "[console]::beep(800,300)" >nul 2>&1
rem 短暂保持颜色让工人辨识，然后恢复默认
timeout /t 1 /nobreak >nul 2>&1
color 07
endlocal
exit /b 0
