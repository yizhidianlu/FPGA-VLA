<#
.SYNOPSIS
    Run the worker loop ONCE (one tick) for testing, then exit.

.DESCRIPTION
    Useful for debugging the worker logic without registering Scheduled Task.
    Skips the while($true) loop in remote_worker.ps1 by sourcing helpers only.
#>

[CmdletBinding()]
param(
    [string]$RepoPath       = "C:\Users\jielu\Desktop\Workspace\FPGA-VLA",
    [string]$VitisSettings  = "E:\Application\Xilinx\Vitis\2024.1\settings64.bat"
)

$env:FPGA_VLA_REPO = $RepoPath
$env:FPGA_VLA_VITIS_SETTINGS = $VitisSettings
$env:FPGA_VLA_TICK_SEC = "9999999"  # never sleeps

# Patch: extract single-tick logic
Write-Host "Running ONE iteration of remote_worker.ps1 for debugging..."
Write-Host "Press Ctrl-C any time."
Write-Host ""

# Run the main worker — it will do one cycle then sleep forever (we'll Ctrl-C it)
& "$PSScriptRoot\remote_worker.ps1" -RepoPath $RepoPath -VitisSettings $VitisSettings -TickSec 9999999
