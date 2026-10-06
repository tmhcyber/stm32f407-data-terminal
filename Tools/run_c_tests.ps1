[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot 'tmp\c-tests'
$testExecutable = Join-Path $outputDirectory 'core_contracts.exe'

$clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($null -ne $clangCommand) {
    $clangPath = $clangCommand.Source
} else {
    $wingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $clangPath = Get-ChildItem -LiteralPath $wingetPackages -Recurse `
        -Filter clang.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'LLVM-MinGW' } |
        Select-Object -First 1 -ExpandProperty FullName
}

if ([string]::IsNullOrWhiteSpace($clangPath)) {
    throw 'clang.exe was not found. Install LLVM or LLVM-MinGW first.'
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$arguments = @(
    '-std=c11'
    '-Wall'
    '-Wextra'
    '-Werror'
    '-pedantic'
    '-O0'
    '-g'
    '-I', (Join-Path $projectRoot 'Components\SHT30\Inc')
    '-I', (Join-Path $projectRoot 'Application\Inc')
    '-I', (Join-Path $projectRoot 'BSP\Inc')
    (Join-Path $projectRoot 'Tests\C\test_core_contracts.c')
    (Join-Path $projectRoot 'Components\SHT30\Src\sht30.c')
    (Join-Path $projectRoot 'Application\Src\app_data_service.c')
    (Join-Path $projectRoot 'Application\Src\app_snapshot_consumer.c')
    (Join-Path $projectRoot 'Application\Src\app_snapshot_text_formatter.c')
    (Join-Path $projectRoot 'Application\Src\app_uplink_service.c')
    (Join-Path $projectRoot 'Application\Src\app_rs485_service.c')
    (Join-Path $projectRoot 'Application\Src\app_system_monitor.c')
    (Join-Path $projectRoot 'Application\Src\app_watchdog_gate.c')
    (Join-Path $projectRoot 'Application\Src\app_uart_bringup.c')
    (Join-Path $projectRoot 'BSP\Src\bsp_system_diagnostics_record.c')
    '-o', $testExecutable
)

Write-Host "Compiler: $clangPath"
& $clangPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "C test compilation failed with exit code $LASTEXITCODE."
}

& $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "C tests failed with exit code $LASTEXITCODE."
}

# Execute the actual CAN diagnostic against a small HAL boundary double.
$canTestExecutable = Join-Path $outputDirectory 'can_loopback.exe'
$canArguments = @(
    '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O0', '-g'
    '-I', (Join-Path $projectRoot 'Tests\CAN\fakes')
    '-I', (Join-Path $projectRoot 'BSP\Inc')
    (Join-Path $projectRoot 'Tests\CAN\test_can_loopback.c')
    '-o', $canTestExecutable
)
& $clangPath @canArguments
if ($LASTEXITCODE -ne 0) {
    throw "CAN diagnostic test compilation failed with exit code $LASTEXITCODE."
}
& $canTestExecutable
if ($LASTEXITCODE -ne 0) {
    throw "CAN diagnostic tests failed with exit code $LASTEXITCODE."
}

$canLinkExecutable = Join-Path $outputDirectory 'can_link.exe'
& $clangPath '-std=c11' '-Wall' '-Wextra' '-Werror' '-pedantic' '-O0' '-g' `
    '-I' (Join-Path $projectRoot 'Tests\CAN\fakes') `
    '-I' (Join-Path $projectRoot 'BSP\Inc') `
    '-I' (Join-Path $projectRoot 'Application\Inc') `
    (Join-Path $projectRoot 'BSP\Src\bsp_can_rx_queue.c') `
    (Join-Path $projectRoot 'Application\Src\app_can_protocol.c') `
    (Join-Path $projectRoot 'Application\Src\app_can_service.c') `
    (Join-Path $projectRoot 'Application\Src\app_data_service.c') `
    (Join-Path $projectRoot 'Tests\CAN\test_can_link.c') '-o' $canLinkExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN external diagnostic test compilation failed.' }
& $canLinkExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN external diagnostic tests failed.' }

$canProtocolExecutable = Join-Path $outputDirectory 'can_protocol.exe'
& $clangPath '-std=c11' '-Wall' '-Wextra' '-Werror' '-pedantic' '-O0' '-g' `
    '-I' (Join-Path $projectRoot 'Application\Inc') `
    (Join-Path $projectRoot 'Application\Src\app_can_protocol.c') `
    (Join-Path $projectRoot 'Application\Src\app_can_service.c') `
    (Join-Path $projectRoot 'Tests\CAN\test_can_protocol.c') '-o' $canProtocolExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN protocol test compilation failed.' }
& $canProtocolExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN protocol tests failed.' }

$canQueueExecutable = Join-Path $outputDirectory 'can_rx_queue.exe'
& $clangPath '-std=c11' '-Wall' '-Wextra' '-Werror' '-pedantic' '-O2' `
    '-I' (Join-Path $projectRoot 'Tests\CAN\queue_fakes') `
    '-I' (Join-Path $projectRoot 'BSP\Inc') `
    (Join-Path $projectRoot 'BSP\Src\bsp_can_rx_queue.c') `
    (Join-Path $projectRoot 'Tests\CAN\test_can_rx_queue.c') '-o' $canQueueExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN RX queue test compilation failed.' }
& $canQueueExecutable
if ($LASTEXITCODE -ne 0) { throw 'CAN RX queue tests failed.' }
