// 공용 명령 ID / 타이머 / 사용자 메시지 (Topbar 버튼, 액셀러레이터, WM_COMMAND
// 공유).
#pragma once
#include <windows.h>

#include <string>
#include <vector>

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
#define IDM_INSERTTOC 112

#define IDT_RENDER 1      // 편집 디바운스: 더티 재평가 + 프리뷰 렌더
#define IDT_SCROLLSYNC 2  // 스크롤 동기화 디바운스 타이머
#define IDT_TOOLTIP 3     // 상단바 버튼 툴팁 표시 지연 타이머
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

// 상단바 버튼 종류. None 은 버튼 없음(액션만, 예: 줌/SAVEAS/CYCLE).
enum class BtnType { None, File, View, WinCtrl, Close };

// 명령 메타데이터의 단일 출처. 한 액션의 라벨/단축키(keymap)와 글리프/툴팁/버튼
// 종류(Topbar)를 한 줄로 모은다. 실행 핸들러는 this 캡처가 필요해 정적 테이블에
// 못 넣으므로 App::buildCommands() 가 IDM 별로 보유한다(디스패치는 이미 단일화).
struct CommandInfo {
  int idm;                  // IDM_* 명령 ID
  const char* actionId;     // 안정 id("save"); 재바인딩 불가(창제어)면 nullptr
  const char* label;        // 설정/단축키 UI 라벨(UTF-8); 없으면 nullptr
  const char* defShortcut;  // 기본 단축키 스펙("Ctrl+S"); 없으면 nullptr
  const wchar_t* glyph;     // 상단바 글리프(Segoe MDL2); 버튼 없으면 nullptr
  const char* tip;          // 상단바 툴팁(UTF-8); 없으면 nullptr
  BtnType btn;              // 상단바 버튼 종류
};

const std::vector<CommandInfo>& commandTable();  // 단일 출처(고정)
const CommandInfo* findCommand(int idm);         // IDM -> 메타(없으면 nullptr)
