@echo off
pip install -r requirements-dev.txt

pyinstaller --noconfirm --clean ^
  --name AgentDashboard ^
  --onedir ^
  --noconsole ^
  --add-data "static;static" ^
  --collect-all uvicorn ^
  --collect-all fastapi ^
  --collect-all starlette ^
  --collect-all zeroconf ^
  --collect-all pynvml ^
  --hidden-import psutil ^
  tray.py

echo.
echo 产物: dist\AgentDashboard\AgentDashboard.exe
pause
