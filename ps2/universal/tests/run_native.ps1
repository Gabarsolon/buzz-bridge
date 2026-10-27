param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$savedPath = $env:PATH
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
    # Host compiler helpers/DLLs need its own bin directory, only for this process.
    $env:PATH = (Split-Path -Parent $compilerPath) + ';' + $env:PATH
    New-Item -ItemType Directory -Force build | Out-Null
    & $compilerPath -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -Isrc `
        -o build/test_native.exe tests/test_native.c src/bb_core.c src/vusb.c
    if ($LASTEXITCODE -ne 0) { throw 'Native test build failed.' }
    & ./build/test_native.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
} finally {
    $env:PATH = $savedPath
    Pop-Location
}
