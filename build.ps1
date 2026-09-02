# ============================================================
#  env_monitor_terminal  Windows 辅助编译脚本 (PowerShell)
#  用途:
#    由于交叉编译器是 aarch64-linux-gnu-gcc (Linux 平台), 本脚本优先使用
#    WSL (Windows Subsystem for Linux) 调用 WSL 内部的 ./build.sh 完成编译。
#  使用:
#    方法1: 右键 -> "使用 PowerShell 运行"
#    方法2: PowerShell 里执行:  .\build.ps1
# ============================================================

$ErrorActionPreference = "Stop"

# 切换到脚本所在目录 (项目根)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  env_monitor_terminal  -  Windows 编译入口" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. 检测 WSL 是否可用
$wslExist = $false
try {
    $wslVer = & wsl.exe --version 2>$null
    if ($LASTEXITCODE -eq 0) {
        $wslExist = $true
        Write-Host "  [OK] 检测到 WSL 环境"
    }
} catch {}

if (-not $wslExist) {
    Write-Host ""
    Write-Host "  [错误] 未检测到 WSL (Windows Subsystem for Linux)" -ForegroundColor Red
    Write-Host "  交叉编译器 aarch64-linux-gnu-gcc 只能运行在 Linux 环境。"
    Write-Host "  请选择以下任一方案:"
    Write-Host "   1. 安装 WSL2 + Ubuntu, 然后在 Ubuntu 里到项目目录执行  ./build.sh"
    Write-Host "   2. 使用 VMware/VirtualBox 装 Ubuntu, 共享项目目录后执行 ./build.sh"
    Write-Host "   3. 直接在 Linux 宿主机上执行 ./build.sh"
    Write-Host ""
    exit 1
}

# 2. 把 Windows 路径转换为 WSL 内部路径, 调用 WSL 里的 bash ./build.sh
$wslPath = & wsl.exe wslpath -a "'$ScriptDir'" 2>$null
# 去除末尾可能的换行
$wslPath = $wslPath -replace "`r|`n",""

Write-Host ""
Write-Host "  -> 进入 WSL 目录: $wslPath"
Write-Host "  -> 执行 bash ./build.sh  (请耐心等待, 首次 cmake + make 较慢)"
Write-Host ""
Write-Host "----------------------------------------------------" -ForegroundColor DarkGray

& wsl.exe --cd "$wslPath" bash ./build.sh

$exitCode = $LASTEXITCODE
Write-Host "----------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""

if ($exitCode -ne 0) {
    Write-Host "  [错误] WSL 内部 build.sh 执行失败, 退出码 $exitCode" -ForegroundColor Red
    exit $exitCode
}

Write-Host "  [完成] WSL 编译 & 打包成功!" -ForegroundColor Green
Write-Host "  输出目录: $ScriptDir\bin\" -ForegroundColor Green
Write-Host "  接下来可以把 bin\ 整个目录 scp 到开发板运行。" -ForegroundColor Green
exit 0
