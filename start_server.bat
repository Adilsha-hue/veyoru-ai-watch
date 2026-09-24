@echo off
REM VEYORU verified Windows launcher. The Gemini key prompt is hidden.
cd /d "%~dp0"
set "LLM_PROVIDER=gemini"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_server.ps1"
pause
