@echo off
REM Double-click to start the GeistAI server (Windows).
cd /d "%~dp0"
if not exist .venv\Scripts\activate.bat (
  echo No .venv found - run the setup from the README first.
  pause
  exit /b 1
)
call .venv\Scripts\activate.bat
python -m geist_server
pause
