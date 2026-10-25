@echo off
if exist "%~dp0buzz-pad-bridge.exe" (
    "%~dp0buzz-pad-bridge.exe" %*
) else (
    python "%~dp0buzz_pad_bridge.py" %*
    pause
)
