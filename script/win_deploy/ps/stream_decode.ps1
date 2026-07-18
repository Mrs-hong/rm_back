# ==========================================================================
# ps/stream_decode.ps1 — plink 远程执行的流式 UTF-8 解码 + 全局超时包装
#
# 设计目标：
#   1. 解决 plink 分块切割多字节 UTF-8 字符（中文）导致的乱码
#      用 .NET 有状态 Decoder 跨分块拼接不完整字节序列
#   2. 提供 wall-clock 超时保护：超时则 Kill plink，返回 124
#      避免设备端卡死时 bat 永久挂起（产线硬性要求）
#   3. PowerShell 2.0 兼容（Win7 默认 PS 2.0）— 仅用 .NET 2.0 API
#
# 输入环境变量（由 lib\ssh.bat exec_long 设置）：
#   PLINK_PATH    plink.exe 完整路径
#   SSH_PASSWORD  SSH 登录密码
#   SSH_PORT      SSH 端口
#   SSH_TARGET    user@host
#   PCBA_CMD      远程命令字符串（含 echo PWD | sudo -S bash pcba_check.sh ...）
#   EXEC_TIMEOUT_SEC  整体超时秒数
#
# 退出码：plink 退出码（=远程命令退出码，pipefail 保证）；124=超时
# ==========================================================================

$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# ---------- 解析超时 ----------
$timeoutSec = 0
if (-not [int]::TryParse($env:EXEC_TIMEOUT_SEC, [ref]$timeoutSec)) { $timeoutSec = 0 }
if ($timeoutSec -le 0) { $timeoutSec = 600 }   # 默认 10 分钟
$timeoutMs = $timeoutSec * 1000

# ---------- 构造 plink 进程 ----------
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $env:PLINK_PATH
# 用 -batch 避免首次 host key 提示（已由 lib\ssh.bat init 预缓存）
$psi.Arguments = ('-batch -pw {0} -P {1} {2} "{3}"' -f `
    $env:SSH_PASSWORD, $env:SSH_PORT, $env:SSH_TARGET, $env:PCBA_CMD)
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true    # RAW 字节流，下面手动解码
$psi.RedirectStandardError = $false    # stderr（sudo 提示等）直接进控制台

$proc = [System.Diagnostics.Process]::Start($psi)
$stream = $proc.StandardOutput.BaseStream

# ---------- 有状态 UTF-8 解码器 ----------
# 跨分块保留不完整字节序列，避免中文被截断成乱码
$decoder = [System.Text.Encoding]::UTF8.GetDecoder()
$buf   = New-Object byte[] 4096
$chars = New-Object char[] 8192

# ---------- 超时计时器 ----------
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$timedOut = $false

# ---------- 异步读循环 ----------
# 用单个 BeginRead 配合 WaitOne(500) 轮询，避免：
#   - 同步 Read 在无输出时永久阻塞（无法检查超时）
#   - 反复 BeginRead 造成 async 资源泄漏
$ar = $stream.BeginRead($buf, 0, $buf.Length, $null, $null)
while ($true) {
    if ($ar.AsyncWaitHandle.WaitOne(500, $false)) {
        # 数据就绪或 EOF
        $n = $stream.EndRead($ar)
        if ($n -le 0) { break }   # EOF
        $cnt = $decoder.GetChars($buf, 0, $n, $chars, 0)
        if ($cnt -gt 0) {
            [Console]::Out.Write($chars, 0, $cnt)
        }
        # 启动下一次读
        $ar = $stream.BeginRead($buf, 0, $buf.Length, $null, $null)
    } else {
        # 500ms 内无数据
        if ($proc.HasExited) {
            # 进程已退出，给挂起的读最后一次机会
            if (-not $ar.AsyncWaitHandle.WaitOne(2000, $false)) {
                break
            }
            # 否则下一轮 EndRead
        } elseif ($sw.ElapsedMilliseconds -ge $timeoutMs) {
            $timedOut = $true
            break
        }
        # 其它情况：继续在同一 $ar 上等待
    }
}

# ---------- 刷出解码器残留字节 ----------
$cnt = $decoder.GetChars($buf, 0, 0, $chars, 0, $true)
if ($cnt -gt 0) {
    [Console]::Out.Write($chars, 0, $cnt)
}

# ---------- 超时清理 ----------
if ($timedOut) {
    [Console]::Error.WriteLine("")
    [Console]::Error.WriteLine("[ERROR] 整体执行超时 (${timeoutSec}s)，已终止远程命令")
    if (-not $proc.HasExited) {
        try { $proc.Kill() } catch { }
        $proc.WaitForExit(3000) | Out-Null
    }
    exit 124
}

$proc.WaitForExit()
exit $proc.ExitCode
