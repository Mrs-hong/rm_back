# ==========================================================================
# run_remote.ps1 - plink remote execution relay with stateful UTF-8 decoding
#
# Why this script exists:
#   plink writes output in arbitrary-sized chunks. When a multi-byte UTF-8
#   character (e.g. Chinese, 3 bytes) is split across two chunks, both the
#   console and PowerShell's default per-chunk decoding render broken
#   characters.
#
# Fix (stateful decoder):
#   Read plink's stdout as a RAW BYTE stream and decode with
#   System.Text.Decoder.GetChars(), which is STATEFUL:
#     - complete UTF-8 sequences are decoded and displayed immediately
#     - incomplete trailing bytes are retained internally and joined with
#       the next chunk  -> no more partial-character garbage
#
# Inputs (environment variables, set by run_check.bat):
#   PLINK_PATH    - full path to plink.exe
#   SSH_PASSWORD  - SSH login password
#   SSH_PORT      - SSH port
#   SSH_TARGET    - user@host
#   PCBA_CMD      - remote command to execute
#
# Exit code: plink's exit code (remote command exit code)
# ==========================================================================

$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# Build plink argument string; PCBA_CMD is quoted as a single remote command
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $env:PLINK_PATH
$psi.Arguments = ('-batch -pw {0} -P {1} {2} "{3}"' -f `
    $env:SSH_PASSWORD, $env:SSH_PORT, $env:SSH_TARGET, $env:PCBA_CMD)
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true   # raw byte stream, decoded below
$psi.RedirectStandardError = $false   # stderr (sudo prompt etc.) goes to console

$proc = [System.Diagnostics.Process]::Start($psi)
$stream = $proc.StandardOutput.BaseStream

# Stateful UTF-8 decoder: incomplete byte sequences are buffered internally
$decoder = [System.Text.Encoding]::UTF8.GetDecoder()
$buf = New-Object byte[] 4096
$chars = New-Object char[] 8192

while (($n = $stream.Read($buf, 0, $buf.Length)) -gt 0) {
    $cnt = $decoder.GetChars($buf, 0, $n, $chars, 0)
    if ($cnt -gt 0) {
        [Console]::Out.Write($chars, 0, $cnt)
    }
}

$proc.WaitForExit()
exit $proc.ExitCode
