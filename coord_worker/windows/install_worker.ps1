<#
.SYNOPSIS
    Install FPGA-VLA Remote Worker on Windows 11 with Claude CLI auto-collaboration.

.DESCRIPTION
    1. Verifies prerequisites: Git, Python, Vitis 2024.1, Claude CLI.
    2. Creates coord/ directory tree if missing.
    3. Pre-configures Claude permissions to allow Vitis + git unattended.
    4. Registers a Scheduled Task to run remote_worker.ps1 at logon + on failure.
    5. Starts the task once immediately.

.PARAMETER RepoPath
    Default: C:\Users\jielu\Desktop\Workspace\FPGA-VLA

.PARAMETER VitisSettings
    Default: E:\Application\Xilinx\Vitis\2024.1\settings64.bat

.PARAMETER TaskName
    Default: FPGA-VLA-Worker (Scheduled Task name)
#>

[CmdletBinding()]
param(
    [string]$RepoPath       = "C:\Users\jielu\Desktop\Workspace\FPGA-VLA",
    [string]$VitisSettings  = "E:\Application\Xilinx\Vitis\2024.1\settings64.bat",
    [string]$TaskName       = "FPGA-VLA-Worker"
)

$ErrorActionPreference = 'Stop'

function Step($msg) { Write-Host "`n[install] $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "  OK  $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "  WARN $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "  FAIL $msg" -ForegroundColor Red; exit 1 }

# ───────── 1. prereq check ─────────

Step "1. Checking prerequisites"

if (-not (Test-Path $RepoPath)) {
    Fail "RepoPath not found: $RepoPath. Run 'git clone https://github.com/yizhidianlu/FPGA-VLA.git $RepoPath' first."
}
Ok "RepoPath: $RepoPath"

if (-not (Test-Path $VitisSettings)) {
    Fail "Vitis settings64.bat not found: $VitisSettings. Pass -VitisSettings to override."
}
Ok "Vitis: $VitisSettings"

foreach ($cmd in @("git", "python", "claude")) {
    $c = Get-Command $cmd -ErrorAction SilentlyContinue
    if (-not $c) { Fail "$cmd not in PATH" }
    Ok "$cmd → $($c.Source)"
}

$pyVer = & python --version 2>&1
Ok $pyVer

# pyyaml needed for task YAML parsing
$pyYaml = & python -c "import yaml; print(yaml.__version__)" 2>&1
if ($LASTEXITCODE -ne 0) {
    Warn "PyYAML not installed; installing..."
    & python -m pip install --quiet --upgrade pyyaml
    if ($LASTEXITCODE -ne 0) { Fail "pip install pyyaml failed" }
    Ok "PyYAML installed"
} else { Ok "PyYAML: $pyYaml" }

# ───────── 2. coord/ dirs ─────────

Step "2. Ensuring coord/ tree"
foreach ($d in @("tasks\pending", "tasks\claimed", "tasks\completed", "tasks\_failed",
                 "reports", "inbox\_processed", "ack\_processed", "heartbeat", "data")) {
    $p = Join-Path $RepoPath "coord\$d"
    if (-not (Test-Path $p)) { New-Item -ItemType Directory -Path $p -Force | Out-Null }
}
Ok "coord/ tree present"

# ───────── 3. Claude CLI permissions ─────────

Step "3. Pre-configuring Claude CLI permissions for unattended execution"
$claudeSettings = Join-Path $env:USERPROFILE ".claude\settings.json"
$claudeDir = Split-Path $claudeSettings -Parent
if (-not (Test-Path $claudeDir)) { New-Item -ItemType Directory -Path $claudeDir -Force | Out-Null }

$allowEntries = @(
    "Bash(git *)",
    "Bash(vitis_hls *)",
    "Bash(vivado *)",
    "Bash(aiecompiler *)",
    "Bash(cmd /c *)",
    "Bash(mv *)", "Bash(cp *)", "Bash(rm *)",
    "Bash(mkdir *)", "Bash(ls *)", "Bash(cat *)",
    "PowerShell(git *)", "PowerShell(vitis_hls *)",
    "PowerShell(vivado *)", "PowerShell(cmd /c *)",
    "Read(*)", "Write(*)", "Edit(*)"
)

if (Test-Path $claudeSettings) {
    $existing = Get-Content $claudeSettings -Raw | ConvertFrom-Json
    if (-not $existing.permissions) {
        $existing | Add-Member -NotePropertyName permissions -NotePropertyValue ([pscustomobject]@{ allow = @() }) -Force
    }
    if (-not $existing.permissions.allow) {
        $existing.permissions | Add-Member -NotePropertyName allow -NotePropertyValue @() -Force
    }
    $cur = @($existing.permissions.allow)
    $merged = ($cur + $allowEntries) | Select-Object -Unique
    $existing.permissions.allow = $merged
    $existing | ConvertTo-Json -Depth 10 | Set-Content -Path $claudeSettings -Encoding UTF8
} else {
    $obj = @{ permissions = @{ allow = $allowEntries } }
    $obj | ConvertTo-Json -Depth 10 | Set-Content -Path $claudeSettings -Encoding UTF8
}
Ok "Claude settings updated: $claudeSettings"

# ───────── 4. git identity (for worker commits) ─────────

Step "4. Git identity check"
$gitEmail = (git config --global user.email 2>$null)
if (-not $gitEmail) {
    git config --global user.email "remote-worker@fpga-vla.local"
    git config --global user.name  "FPGA-VLA Remote Worker"
    Ok "Set git identity"
} else { Ok "Git identity: $gitEmail" }

# ───────── 5. Scheduled Task registration ─────────

Step "5. Registering Scheduled Task '$TaskName'"

$workerScript = Join-Path $RepoPath "coord_worker\windows\remote_worker.ps1"
if (-not (Test-Path $workerScript)) { Fail "Worker script not found: $workerScript" }

$arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$workerScript`" -RepoPath `"$RepoPath`" -VitisSettings `"$VitisSettings`""

$action  = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arguments
$triggerLogon  = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit (New-TimeSpan -Days 30)
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest

$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existing) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Ok "Removed existing task"
}

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Trigger $triggerLogon `
    -Settings $settings `
    -Principal $principal `
    -Description "FPGA-VLA Remote Worker: polls git, invokes Claude CLI per task." | Out-Null
Ok "Registered Scheduled Task: $TaskName"

# Start it now
Start-ScheduledTask -TaskName $TaskName
Start-Sleep -Seconds 5
$state = (Get-ScheduledTask -TaskName $TaskName).State
Ok "Task state: $state"

# ───────── 6. wrap-up ─────────

Step "6. Done"

Write-Host @"

══════════════════════════════════════════════════════════════════════
  FPGA-VLA Remote Worker installed and running.

  ▸ Worker:           Scheduled Task '$TaskName'
  ▸ Restart policy:   auto-restart 999× at 1-min interval
  ▸ Daemon script:    $workerScript
  ▸ Log:              Task Scheduler history (Event Viewer)

  Useful commands:
    Get-ScheduledTask  -TaskName $TaskName
    Get-ScheduledTaskInfo -TaskName $TaskName
    Stop-ScheduledTask  -TaskName $TaskName
    Start-ScheduledTask -TaskName $TaskName
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:`$false

  Live monitoring (run separately):
    Get-Content (Join-Path '$RepoPath' 'coord\heartbeat\remote_claude.json') -Wait

  First task T0010_env_probe will be claimed in next 30s.
  Watch coord/inbox/ for DONE notification once Claude completes it.

══════════════════════════════════════════════════════════════════════
"@ -ForegroundColor Green
