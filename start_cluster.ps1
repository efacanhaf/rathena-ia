# DimensionsRO ia-server cluster bootstrap.
# Stops any running map/ai, starts map hidden, waits for "ready" in log,
# then starts ai-server hidden. Compatible with Windows PowerShell 5.1.

param([int]$BootTimeoutSeconds = 300)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$prefix = '[start_cluster]'

Write-Host "$prefix root=$root"

# 1. Stop existing
$existing = Get-Process map-server, ai-server -ErrorAction SilentlyContinue
foreach ($p in $existing) {
    Write-Host ($prefix + ' stopping ' + $p.Name + ' pid ' + $p.Id)
    Stop-Process -Id $p.Id -Force
}
Start-Sleep -Seconds 2

# 2. Start map-server hidden
$mapOut = Join-Path $root 'log\map-bg.out'
$mapErr = Join-Path $root 'log\map-bg.err'
$map = Start-Process -FilePath (Join-Path $root 'map-server.exe') -WorkingDirectory $root -RedirectStandardOutput $mapOut -RedirectStandardError $mapErr -WindowStyle Hidden -PassThru
Write-Host ($prefix + ' map-server pid=' + $map.Id + ' (waiting up to ' + $BootTimeoutSeconds + 's for ready)')

# 3. Poll log for ready
$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if ($map.HasExited) {
        $ec = '0x{0:X8}' -f $map.ExitCode
        Write-Error ($prefix + ' map-server exited before ready (code ' + $ec + ')')
        exit 1
    }
    if ((Test-Path $mapOut) -and (Select-String -Path $mapOut -Pattern 'Server is .ready|ready\.' -Quiet)) {
        $ready = $true
        break
    }
    Start-Sleep -Seconds 3
}
if (-not $ready) {
    Write-Error ($prefix + ' timeout waiting for map-server ready')
    exit 1
}
Write-Host ($prefix + ' map-server ready')

# 4. Start ai-server hidden
$aiOut = Join-Path $root 'log\ai-bg.out'
$aiErr = Join-Path $root 'log\ai-bg.err'
$ai = Start-Process -FilePath (Join-Path $root 'ai-server.exe') -WorkingDirectory $root -RedirectStandardOutput $aiOut -RedirectStandardError $aiErr -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 4
if ($ai.HasExited) {
    $ec = '0x{0:X8}' -f $ai.ExitCode
    Write-Error ($prefix + ' ai-server exited (code ' + $ec + ')')
    exit 1
}
Write-Host ($prefix + ' ai-server pid=' + $ai.Id + ' up')
Write-Host ($prefix + ' DONE map=' + $map.Id + ' ai=' + $ai.Id)
