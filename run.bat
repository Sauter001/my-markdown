@echo off
rem MyMD 실행 (인자로 .md 파일 경로를 주면 해당 파일을 열고 시작)
setlocal
set "ROOT=%~dp0"
start "" "%ROOT%MyMD.exe" %*
