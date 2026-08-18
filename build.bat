@echo off
pip install pyinstaller pystray pillow fastapi "uvicorn[standard]" psutil nvidia-ml-py

pyinstaller --noconfirm --clean ^
  --name AgentDashboard ^
  --onefile ^
  --noconsole ^
  --add-data "static;static" ^
  --collect-all uvicorn ^
  --collect-all fastapi ^
  --collect-all starlette ^
  --collect-all pynvml ^
  --hidden-import psutil ^
  tray.py

echo.
echo 产物: dist\AgentDashboard.exe
pause
