param([string]$Sdk = 'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'plugin/build_ps3.ps1') -Sdk $Sdk
if ($LASTEXITCODE) { throw 'Build failed' }
