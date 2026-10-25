@echo off
REM GeistAI server setup for Windows. Double-click it. Safe to run again.
setlocal
cd /d "%~dp0"
echo.
echo  ==== GeistAI server setup ====
echo.

REM ---- 1. find Python 3.10 - 3.13 (the speech engine has no packages for newer versions yet)
set "PY="
for %%V in (3.13 3.12 3.11 3.10) do (
  if not defined PY (
    py -%%V -c "import sys" >nul 2>&1 && set "PY=py -%%V"
  )
)
if defined PY goto have_python
python -c "import sys; exit(0 if (3,10) <= sys.version_info[:2] <= (3,13) and sys.maxsize > 2**32 else 1)" >nul 2>&1 && set "PY=python"
if defined PY goto have_python
echo  Python 3.13 is missing. Install it with this command, then run setup.bat again:
echo.
echo      winget install -e --id Python.Python.3.13
echo.
echo  Or download "Python 3.13" (64-bit^) from https://www.python.org/downloads/windows/
goto fail

:have_python
echo  [1/4] Python: %PY%

REM ---- 2. virtual environment + packages
if not exist .venv\Scripts\python.exe (
  %PY% -m venv .venv || goto fail
)
set "VPY=.venv\Scripts\python.exe"
echo  [2/4] Installing packages - this takes a few minutes the first time ...
%VPY% -m pip install --upgrade pip --quiet
%VPY% -m pip install -r requirements.txt || goto fail

REM ---- 3. NVIDIA graphics card: much faster speech recognition
where nvidia-smi >nul 2>&1 || goto no_gpu
echo  [3/4] NVIDIA graphics card found - installing GPU support ...
%VPY% -m pip install -r requirements-gpu.txt || echo  GPU support failed, the CPU will be used instead.
goto questions
:no_gpu
echo  [3/4] No NVIDIA graphics card - speech recognition runs on the CPU.

REM ---- 4. questions, voices, language model
:questions
echo  [4/4] Settings, voices and language model
%VPY% -m geist_server.firstrun || goto fail

echo.
echo  All done! Start GeistAI with start.bat
echo  When Windows asks about the firewall, allow access on PRIVATE networks.
echo.
pause
exit /b 0

:fail
echo.
echo  Setup stopped because of the error above.
pause
exit /b 1
