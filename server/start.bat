@echo off
REM Double-click to start the GeistAI server (Windows).
cd /d "%~dp0"
if not exist .venv\Scripts\activate.bat (
  echo No .venv found - double-click setup.bat first.
  pause
  exit /b 1
)
call .venv\Scripts\activate.bat
python -m geist_server
pause
