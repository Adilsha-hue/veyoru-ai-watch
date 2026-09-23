@echo off
REM VEYORU verified Windows launcher. The Groq key prompt is hidden.
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_server.ps1"
pause
