@echo off
rem MyMD 빌드 스크립트 (MinGW-w64 / g++, 정적 링크 standalone exe)
setlocal
cd /d "%~dp0"
set "GXX=third_party\mingw64\bin\g++.exe"
set "WINDRES=third_party\mingw64\bin\windres.exe"

if not exist "%GXX%" (
  echo [오류] MinGW g++ 를 찾을 수 없습니다: %GXX%
  echo third_party\mingw64 가 준비되어 있는지 확인하세요.
  exit /b 1
)

rem 리소스 컴파일 (favicon.ico 아이콘 임베드).
rem windres 내부 전처리기가 경로 공백에서 깨지므로 루트 기준 상대 경로로 호출한다.
if not exist build mkdir build
"%WINDRES%" -I. -Isrc src\resource.rc -O coff -o build\resource.o
if errorlevel 1 (
  echo 리소스 컴파일 실패
  exit /b 1
)

"%GXX%" -std=c++17 -O2 -DUNICODE -D_UNICODE ^
  src\main.cpp ^
  build\resource.o ^
  -Ithird_party\webview ^
  -Ithird_party\webview2\build\native\include ^
  -static -static-libgcc -static-libstdc++ ^
  -mwindows ^
  -o MyMD.exe ^
  -lole32 -loleaut32 -lshlwapi -lshell32 -luser32 -lgdi32 -ladvapi32 -lversion -lcomdlg32

if errorlevel 1 (
  echo 빌드 실패
  exit /b 1
)
echo 빌드 완료: %CD%\MyMD.exe
