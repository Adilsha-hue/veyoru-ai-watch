@echo off
REM VEYORU lab launcher (Windows) - double-click this file.
REM Serves index.html on http://localhost:8000 with blank preview until board connects.
setlocal
cd /d %~dp0
where py >nul 2>nul
if %errorlevel%==0 (
  if defined GROQ_API_KEY (
    echo VEYORU lab with Groq free cloud...
    py -3 server.py --port 8000
  ) else (
    echo VEYORU lab in local-demo mode (no GROQ_API_KEY set)...
    echo For real AI: set GROQ_API_KEY=gsk_... ^& start_server.bat
    py -3 server.py --port 8000
  )
) else (
  python server.py --port 8000
)
pause
