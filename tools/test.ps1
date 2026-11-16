param([string]$Lua = 'lua')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    foreach ($test in @('server_indicators_test.lua', 'resources_test.lua', 'colors_test.lua', 'config_test.lua', 'engine_test.lua', 'renderer_test.lua', 'addon_test.lua')) {
        & $Lua (Join-Path 'tests' $test)
        if ($LASTEXITCODE -ne 0) { throw "$test failed" }
    }
} finally { Pop-Location }
