@echo off
start "" TankNet.exe server
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player1 127.0.0.1
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player2 127.0.0.1
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player3 127.0.0.1
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player4 127.0.0.1
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player5 127.0.0.1
timeout /t 1 /nobreak >nul
start "" TankNet.exe client Player6 127.0.0.1