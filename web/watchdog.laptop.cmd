@echo off
rem Starts the website again if caddy is not running. Runs every 5 minutes as SYSTEM.
tasklist /fi "imagename eq caddy.exe" | find /i "caddy.exe" >nul
if errorlevel 1 schtasks /run /tn "Squatch Mesh website" >nul
