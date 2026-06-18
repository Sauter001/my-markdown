@echo off
rem MyMD 빌드 스크립트 (MinGW-w64 / g++, 정적 링크 standalone exe)
setlocal
set "ROOT=%~dp0"
set "GXX=%ROOT%third_party\mingw64\bin\g++.exe"

if not exist "%GXX%" (
  echo [오류] MinGW g++ 를 찾을 수 없습니다: %GXX%
  echo third_party\mingw64 가 준비되어 있는지 확인하세요.
  exit /b 1
)

"%GXX%" -std=c++17 -O2 -DUNICODE -D_UNICODE ^
  "%ROOT%src\main.cpp" ^
  -I"%ROOT%third_party\webview" ^
  -I"%ROOT%third_party\webview2\build\native\include" ^
  -static -static-libgcc -static-libstdc++ ^
  -mwindows ^
  -o "%ROOT%MyMD.exe" ^
  -lole32 -loleaut32 -lshlwapi -lshell32 -luser32 -lgdi32 -ladvapi32 -lversion -lcomdlg32

if errorlevel 1 (
  echo 빌드 실패
  exit /b 1
)
echo 빌드 완료: %ROOT%MyMD.exe
