@echo off
if exist "%~dp0buzz-bridge-ps2.exe" (
    "%~dp0buzz-bridge-ps2.exe" %*
) else (
    python "%~dp0buzz_bridge_ps2.py" %*
    pause
)
