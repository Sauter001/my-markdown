// 공용 명령 ID / 타이머 / 사용자 메시지 (Topbar 버튼, 액셀러레이터, WM_COMMAND
// 공유).
#pragma once
#include <windows.h>

// 명령 ID (단축키/메뉴/버튼)
#define IDM_NEW 101
#define IDM_OPEN 102
#define IDM_SAVE 103
#define IDM_SAVEAS 104
#define IDM_VSCODE 105
#define IDM_INSERTTABLE 106
#define IDM_FORMATTABLE 107
#define IDM_SETTINGS 108
#define IDM_ZOOM_IN 109
#define IDM_ZOOM_OUT 110
#define IDM_ZOOM_RESET 111

#define IDT_RENDER 1      // 편집 디바운스: 더티 재평가 + 프리뷰 렌더
#define IDT_SCROLLSYNC 2  // 스크롤 동기화 디바운스 타이머
#define WM_APP_PREWARM \
  (WM_APP + 1)  // 첫 페인트 후 WebView2 엔진 백그라운드 프리웜

// 창 제어 + 보기 세그먼트 버튼 ID
enum {
  IDM_MIN = 201,
  IDM_MAX = 202,
  IDM_WCLOSE = 203,
  IDM_VIEW_E = 301,
  IDM_VIEW_S = 302,
  IDM_VIEW_P = 303,
  IDM_CYCLE = 304  // 보기 순환 (편집 -> 분할 -> 미리보기 -> ...)
};

// 보기 모드 (0 에디터, 1 분할, 2 미리보기) - 매직 정수 의미 명시
enum { VIEW_EDITOR = 0, VIEW_SPLIT = 1, VIEW_PREVIEW = 2 };
