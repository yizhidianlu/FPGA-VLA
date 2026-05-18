<#
.SYNOPSIS
    FPGA-VLA Remote Worker — PowerShell scheduler + Claude CLI executor.

.DESCRIPTION
    Polls the FPGA-VLA git repo every TICK_SEC seconds.
    When a pending task has all deps satisfied, invokes Claude CLI to execute it.
    Claude CLI reads the task YAML, runs Vitis/Vivado, writes result.json + DONE inbox,
    and pushes commits. PowerShell daemon just orchestrates timing and heartbeat.

.NOTES
    Designed for Windows 11 + Claude CLI installed + Vitis 2024.1 installed.
    Vitis env is sourced into the daemon process at startup (settings64.bat).

.PARAMETER RepoPath
    Absolute path to the cloned FPGA-VLA repo.

.PARAMETER VitisSettings
    Absolute path to Xilinx Vitis settings64.bat.

.PARAMETER TickSec
    Polling interval in seconds. Default 30.
#>

[CmdletBinding()]
param(
    [string]$RepoPath       = $env:FPGA_VLA_REPO,
    [string]$VitisSettings  = $env:FPGA_VLA_VITIS_SETTINGS,
    [int]   $TickSec        = $(if ($env:FPGA_VLA_TICK_SEC) { [int]$env:FPGA_VLA_TICK_SEC } else { 30 }),
    [string]$WorkerId       = $(if ($env:FPGA_VLA_WORKER_ID) { $env:FPGA_VLA_WORKER_ID } else { "remote_claude_win" })
)

if (-not $RepoPath)      { $RepoPath      = "C:\Users\jielu\Desktop\Workspace\FPGA-VLA" }
if (-not $VitisSettings) { $VitisSettings = "E:\Application\Xilinx\Vitis\2024.1\settings64.bat" }

$ErrorActionPreference = 'Continue'
$ProtocolVersion = "1.0"
$StartTs = Get-Date

# ───────── helpers ─────────

function Log {
    param([string]$Message)
    $stamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    Write-Host "[$stamp] $Message"
}

function IsoNow      { (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ") }
function IsoNowSafe  { (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH-mm-ssZ") }

function Source-VitisEnv {
    if (-not (Test-Path $VitisSettings)) {
        Log "WARN: Vitis settings64.bat not found at $VitisSettings"
        return
    }
    Log "Sourcing Vitis env from $VitisSettings"
    $envDump = cmd /c "`"$VitisSettings`" >NUL 2>&1 && set"
    foreach ($line in $envDump) {
        if ($line -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
    $hlsCheck = (Get-Command vitis_hls.bat -ErrorAction SilentlyContinue) `
              -or (Get-Command vitis_hls -ErrorAction SilentlyContinue)
    if ($hlsCheck) { Log "vitis_hls available: OK" } else { Log "WARN: vitis_hls NOT in PATH after sourcing" }
}

function Git-SafePull {
    Push-Location $RepoPath
    try {
        for ($i = 1; $i -le 3; $i++) {
            $r = git pull --rebase --autostash --quiet 2>&1
            if ($LASTEXITCODE -eq 0) { return $true }
            Log "git pull failed (attempt $i): $r"
            Start-Sleep -Seconds 5
        }
        return $false
    } finally { Pop-Location }
}

function Git-SafePush {
    param([string]$Message = "worker $WorkerId tick")
    Push-Location $RepoPath
    try {
        git add coord/ 2>$null | Out-Null
        $diff = git diff --cached --quiet
        if ($LASTEXITCODE -eq 0) { return $true }  # nothing staged
        git commit -m $Message --quiet 2>&1 | Out-Null
        for ($i = 1; $i -le 3; $i++) {
            git push --quiet 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) { return $true }
            Log "git push failed (attempt $i), pulling+rebasing"
            git pull --rebase --autostash --quiet 2>&1 | Out-Null
            Start-Sleep -Seconds 3
        }
        return $false
    } finally { Pop-Location }
}

function Write-Heartbeat {
    param([string]$CurrentTask = "")
    $hbFile = Join-Path $RepoPath "coord\heartbeat\remote_claude.json"
    $uptime = [int]((Get-Date) - $StartTs).TotalSeconds
    $vitis  = if (Test-Path $VitisSettings) { "OK" } else { "MISSING" }
    $vivado = if (Get-Command vivado -ErrorAction SilentlyContinue) { "OK" } else { "MISSING" }
    $claude = if (Get-Command claude -ErrorAction SilentlyContinue) { "OK" } else { "MISSING" }
    $gpu = (nvidia-smi --query-gpu=name --format=csv,noheader 2>$null | Select-Object -First 1)
    if (-not $gpu) { $gpu = "none" }
    $hb = @{
        worker = $WorkerId
        platform = "windows"
        ts = (IsoNow)
        pid = $PID
        uptime_sec = $uptime
        vitis  = $vitis
        vivado = $vivado
        claude_cli = $claude
        gpu    = $gpu
        current_task = $CurrentTask
        protocol_version = $ProtocolVersion
    }
    $hb | ConvertTo-Json -Depth 4 | Set-Content -Path $hbFile -Encoding UTF8
}

function Check-ProtocolVersion {
    $proto = Join-Path $RepoPath "coord\PROTOCOL.md"
    if (-not (Test-Path $proto)) { return $true }
    $line = Select-String -Path $proto -Pattern 'PROTOCOL_VERSION\s*=\s*"([^"]+)"' | Select-Object -First 1
    if (-not $line) { return $true }
    $v = $line.Matches[0].Groups[1].Value
    if ($v -ne $ProtocolVersion) {
        Log "ERROR: PROTOCOL.md version=$v but worker expects $ProtocolVersion — halting"
        return $false
    }
    return $true
}

function Get-EligibleTask {
    # Returns the path of the first pending task whose depends_on are all in completed/.
    $pendDir = Join-Path $RepoPath "coord\tasks\pending"
    $compDir = Join-Path $RepoPath "coord\tasks\completed"
    if (-not (Test-Path $pendDir)) { return $null }
    $pending = Get-ChildItem -Path $pendDir -Filter "*.yaml" -ErrorAction SilentlyContinue |
               Sort-Object Name
    foreach ($f in $pending) {
        # Parse deps via python (already a dep)
        $deps = python -c "import yaml, sys; t=yaml.safe_load(open(sys.argv[1])); print(','.join(t.get('depends_on') or []))" $f.FullName 2>$null
        $ok = $true
        if ($deps) {
            foreach ($d in $deps.Split(',')) {
                if ($d -eq "") { continue }
                $match = Get-ChildItem -Path $compDir -Filter "$d*.yaml" -ErrorAction SilentlyContinue
                if (-not $match) { $ok = $false; break }
            }
        }
        if ($ok) { return $f }
    }
    return $null
}

function Process-Acks {
    $ackDir = Join-Path $RepoPath "coord\ack"
    $procDir = Join-Path $ackDir "_processed"
    if (-not (Test-Path $procDir)) { New-Item -ItemType Directory -Path $procDir | Out-Null }
    Get-ChildItem -Path $ackDir -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
        Log "ACK: $($_.Name)"
        # For Windows v1, we just archive ACKs; Claude handles complex ACK logic via its own prompts
        Move-Item -Path $_.FullName -Destination (Join-Path $procDir $_.Name) -Force
    }
}

function Invoke-ClaudeOnTask {
    param([System.IO.FileInfo]$TaskFile)
    $tid = ($TaskFile.BaseName -split '_')[0]
    Log "CLAIMED $tid → invoking Claude CLI"
    Write-Heartbeat -CurrentTask $tid

    # Atomically claim
    $claimedDir = Join-Path $RepoPath "coord\tasks\claimed"
    $claimedPath = Join-Path $claimedDir $TaskFile.Name
    try {
        Move-Item -Path $TaskFile.FullName -Destination $claimedPath -ErrorAction Stop
    } catch {
        Log "  claim failed (raced): $_"
        return
    }
    Git-SafePush "claim $tid by $WorkerId" | Out-Null

    # Build the per-task prompt
    $protocolText = Get-Content (Join-Path $RepoPath "coord\PROTOCOL.md") -Raw
    $taskText     = Get-Content $claimedPath -Raw
    $reportDir    = Join-Path $RepoPath "coord\reports\$tid"
    $runDir       = Join-Path $RepoPath "runs\$tid"
    New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null

    $prompt = @"
You are FPGA-VLA Remote Worker (Claude CLI on Windows 11).

# Repository
$RepoPath  (you are running here)

# Vitis 2024.1 environment
Already sourced into your process env. Use vitis_hls.bat / vivado.bat directly.
If env got lost, run: cmd /c "`"$VitisSettings`" && set" and re-import.

# Your task
A claimed task is at: coord\tasks\claimed\$($TaskFile.Name)
Work dir for scratch: runs\$tid\   (not in git)
Report dir for artefacts: coord\reports\$tid\   (will be committed)

# Task YAML content
``````yaml
$taskText
``````

# What you MUST do
1. Read coord/PROTOCOL.md §7 to understand the task type's handler spec.
2. cd into runs\$tid\ for scratch work.
3. Execute the task per its `type` field. Generate any needed source files,
   TCL scripts, etc. Run vitis_hls / vivado as appropriate.
4. Write coord/reports/$tid/stdout.log (tee your runs)
5. Write coord/reports/$tid/result.json conforming to coord/schemas/result.schema.json
   with status (DONE/FAILED/TIMEOUT), result fields per task type, and
   acceptance_criteria_passed (evaluate task.acceptance_criteria yourself).
6. Copy any .rpt / .tcl / .log artefacts into coord/reports/$tid/
7. Generate inbox notification:
   coord/inbox/$(IsoNowSafe)_${tid}_DONE.json   (or _FAIL.json)
   Contents: copy of result.json
8. Move task: coord\tasks\claimed\$($TaskFile.Name) → coord\tasks\completed\
9. git add coord runs/$tid (only commit coord/, runs/ is .gitignored)
10. git commit -m "task $tid complete: <one-line summary>"
11. git push

# Honesty / safety
- Never fabricate vitis_hls/vivado output. If a tool failed, mark status=FAILED.
- If acceptance criteria don't pass, set acceptance_criteria_passed=false and human_ack_needed=true; do not auto-create a follow-up task.
- Timebox: this task has deadline_minutes in its YAML; if you're approaching it, finalize result.json as TIMEOUT and exit.

# When done, your final assistant message should print the result.json contents.
Begin now.
"@

    # Save prompt for audit
    $promptFile = Join-Path $reportDir "_claude_prompt.txt"
    Set-Content -Path $promptFile -Value $prompt -Encoding UTF8

    # Invoke Claude CLI in non-interactive mode
    $logFile = Join-Path $reportDir "_claude_session.log"
    $deadlineMin = (python -c "import yaml; print(yaml.safe_load(open(r'$claimedPath')).get('deadline_minutes',60))" 2>$null)
    if (-not $deadlineMin) { $deadlineMin = 60 }
    $deadlineSec = [int]$deadlineMin * 60

    Log "  Claude session begin (timeout ${deadlineMin}min, log → $logFile)"
    Push-Location $RepoPath
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "claude"
        # --print: non-interactive single-shot; --dangerously-skip-permissions for unattended
        $psi.ArgumentList.Add("--print")
        $psi.ArgumentList.Add("--dangerously-skip-permissions")
        $psi.ArgumentList.Add($prompt)
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError  = $true
        $psi.WorkingDirectory = $RepoPath
        $proc = [System.Diagnostics.Process]::Start($psi)
        $finished = $proc.WaitForExit($deadlineSec * 1000)
        if (-not $finished) {
            $proc.Kill()
            Log "  Claude session TIMEOUT after ${deadlineMin}min"
            "TIMEOUT: claude session exceeded ${deadlineMin}min" | Set-Content $logFile
        } else {
            $stdout = $proc.StandardOutput.ReadToEnd()
            $stderr = $proc.StandardError.ReadToEnd()
            "===STDOUT===`n$stdout`n===STDERR===`n$stderr" | Set-Content $logFile
            Log "  Claude exit=$($proc.ExitCode)"
        }
    } finally { Pop-Location }

    # Sync any commits Claude made
    Git-SafePush "post-claude $tid" | Out-Null
    Write-Heartbeat -CurrentTask ""
}

# ───────── startup ─────────

Log "=== FPGA-VLA Worker $WorkerId starting (Windows) ==="
Log "  Repo: $RepoPath"
Log "  Vitis: $VitisSettings"
Log "  Tick: ${TickSec}s    Protocol: $ProtocolVersion"

if (-not (Test-Path $RepoPath)) { Log "FATAL: RepoPath not found"; exit 1 }
if (-not (Get-Command claude -ErrorAction SilentlyContinue)) { Log "FATAL: claude CLI not in PATH"; exit 1 }

Source-VitisEnv

# Ensure all coord dirs exist
foreach ($d in @("tasks\pending", "tasks\claimed", "tasks\completed", "tasks\_failed",
                 "reports", "inbox\_processed", "ack\_processed", "heartbeat", "data")) {
    $p = Join-Path $RepoPath "coord\$d"
    if (-not (Test-Path $p)) { New-Item -ItemType Directory -Path $p -Force | Out-Null }
}

# ───────── main loop ─────────

while ($true) {
    try {
        if (-not (Git-SafePull)) { Start-Sleep -Seconds $TickSec; continue }
        if (-not (Check-ProtocolVersion)) { Start-Sleep -Seconds 60; continue }
        Write-Heartbeat
        Process-Acks
        $task = Get-EligibleTask
        if ($task) {
            Invoke-ClaudeOnTask -TaskFile $task
        }
        Git-SafePush "heartbeat $WorkerId" | Out-Null
    } catch {
        Log "MAIN LOOP ERROR: $_"
    }
    Start-Sleep -Seconds $TickSec
}
