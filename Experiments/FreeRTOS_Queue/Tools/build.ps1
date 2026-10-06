[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$KeilPath)

$ErrorActionPreference = 'Stop'
$projectDir = Join-Path $PSScriptRoot '..\MDK-ARM'
$keil = $KeilPath
if (-not (Test-Path -LiteralPath $keil)) { throw "Keil not found: $keil" }
Push-Location $projectDir
try {
    & $keil -r '.\FreeRTOS_Queue.uvprojx' -t 'FreeRTOS_Queue' -j0 -o '.\build.log'
    $buildExit = $LASTEXITCODE
    $log = Get-Content -LiteralPath '.\build.log' -Raw
    if (($buildExit -ne 0) -or ($log -notmatch '0 Error\(s\), 0 Warning\(s\)')) {
        throw "Build did not finish with 0 errors and 0 warnings (exit $buildExit). See MDK-ARM/build.log."
    }
} finally {
    Pop-Location
}
