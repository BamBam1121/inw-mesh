@echo off
rem Starts the website and the help desk again if either is not running. Runs every minute as SYSTEM.
tasklist /fi "imagename eq caddy.exe" | find /i "caddy.exe" >nul
if errorlevel 1 schtasks /run /tn "Squatch Mesh website" >nul
curl.exe -s -m 5 -o nul http://127.0.0.1:8090/api/help/status
if errorlevel 1 schtasks /run /tn "Squatch Mesh helpdesk" >nul
