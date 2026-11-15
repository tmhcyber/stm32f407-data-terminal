[CmdletBinding()]
param()

# Fixed, named entry for the verified Modbus + diagnostic demonstration.
# This compiles only. Flashing remains a separate, explicit hardware action.
$ErrorActionPreference = 'Stop'
$modbusProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $modbusProjectRoot
try {
    & python -X utf8 -B (Join-Path $PSScriptRoot 'build_modbus.py') --mode 1 --diagnostics 1
    if ($LASTEXITCODE -ne 0) {
        throw "Modbus demonstration build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
