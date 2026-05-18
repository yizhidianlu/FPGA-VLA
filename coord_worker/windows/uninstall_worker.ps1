<# Uninstall the FPGA-VLA worker Scheduled Task. #>
[CmdletBinding()]
param(
    [string]$TaskName = "FPGA-VLA-Worker"
)
$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($task) {
    Stop-ScheduledTask  -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "Removed Scheduled Task: $TaskName" -ForegroundColor Green
} else {
    Write-Host "No Scheduled Task named $TaskName found." -ForegroundColor Yellow
}
