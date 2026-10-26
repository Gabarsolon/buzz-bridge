param([string]$Sdk = 'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
$saved = @{}
foreach ($key in @('PS3DK','PS3DEV','PSL1GHT','PATH','TEMP','TMP','PYTHONDONTWRITEBYTECODE')) { $saved[$key] = [Environment]::GetEnvironmentVariable($key,'Process') }
try {
  $env:PS3DK = $Sdk
  $env:PS3DEV = $Sdk.Replace('\','/')
  $env:PSL1GHT = $env:PS3DEV
  $env:PATH = "$Sdk\bin;$Sdk\ppu\bin;$env:PATH"
  $env:TEMP = Join-Path $PWD 'build\tmp'; $env:TMP = $env:TEMP
  $env:PYTHONDONTWRITEBYTECODE = '1'
  New-Item -ItemType Directory -Force -Path build/tmp | Out-Null
  python -B plugin/tools/build_autoload.py
  if ($LASTEXITCODE) { throw 'Native build/package verification failed' }
} finally {
  foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key,$saved[$key],'Process') }
  Pop-Location
}
