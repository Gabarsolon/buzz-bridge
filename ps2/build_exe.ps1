# Build a console, single-file PyInstaller exe. Run from any directory.
param([string]$DistPath = '.')

$ErrorActionPreference = 'Stop'
$savedConfig = $env:PYINSTALLER_CONFIG_DIR
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
$savedBytecode = $env:PYTHONDONTWRITEBYTECODE
Push-Location $PSScriptRoot
try {
    $outputDirectory = if ([System.IO.Path]::IsPathRooted($DistPath)) {
        [System.IO.Path]::GetFullPath($DistPath)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $DistPath))
    }
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $exePath = Join-Path $outputDirectory 'buzz-bridge-ps2.exe'
    if (Test-Path -LiteralPath $exePath) {
        # Probe without changing bytes. A running Windows executable cannot be
        # opened for writing; report it without stopping any process.
        try {
            $probe = [System.IO.File]::Open($exePath, [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
            $probe.Dispose()
        } catch {
            throw "Cannot rebuild $exePath`: the exe is locked/in use or read-only. Close it or make it writable, then rerun. No process was stopped. $($_.Exception.Message)"
        }
    }
    New-Item -ItemType Directory -Force build/tmp | Out-Null
    $env:PYINSTALLER_CONFIG_DIR = Join-Path $PSScriptRoot 'build/pyinstaller-cache'
    $env:TEMP = Join-Path $PSScriptRoot 'build/tmp'
    $env:TMP = $env:TEMP
    $env:PYTHONDONTWRITEBYTECODE = '1'
    python -m PyInstaller --version
    if ($LASTEXITCODE -ne 0) {
        throw 'PyInstaller is not installed for this Python. No packages were installed.'
    }
    # PyInstaller resolves data sources relative to its generated spec directory.
    # Resolve from this script so --specpath build works from any working directory.
    $moduleData = (Join-Path $PSScriptRoot 'universal/bbusbd.irx') + ';universal'
    $noticeData = (Join-Path $PSScriptRoot 'universal/THIRD_PARTY.md') + ';universal'
    $afl2Data = (Join-Path $PSScriptRoot 'universal/licenses/AFL-2.0.txt') + ';universal/licenses'
    $afl3Data = (Join-Path $PSScriptRoot 'universal/licenses/AFL-3.0.txt') + ';universal/licenses'
    $mitData = (Join-Path $PSScriptRoot '../LICENSE') + ';.'
    python -m PyInstaller --noconfirm --clean --onefile --console --name buzz-bridge-ps2 `
        --distpath $outputDirectory --workpath build/pyinstaller --specpath build `
        --add-data $moduleData --add-data $noticeData --add-data $afl2Data `
        --add-data $afl3Data --add-data $mitData buzz_bridge_ps2.py
    if ($LASTEXITCODE -ne 0) { throw 'PyInstaller build failed.' }
    Get-Item -LiteralPath $exePath | Select-Object FullName,Length
} finally {
    $env:PYINSTALLER_CONFIG_DIR = $savedConfig
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
    $env:PYTHONDONTWRITEBYTECODE = $savedBytecode
    Pop-Location
}
