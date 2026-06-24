// 명령 메타데이터 단일 출처(B1). keymap(라벨/단축키)과 Topbar(글리프/툴팁/종류)이
// 이 표를 읽어 생성한다. 행 순서는 keymap 동작 목록 순서(설정 단축키 목록 표시
// 순서, preview.js 동기화 순서)를 따른다. ACCEL/preview.js 매칭은 순서 무관이며
// 상단바 배치 순서는 Topbar::layout 이 IDM 배열로 따로 정한다.
#include "commands.h"

const std::vector<CommandInfo>& commandTable() {
  // clang-format off
  static const std::vector<CommandInfo> t = {
      // idm,            actionId,       label(UTF-8),             defShortcut,    glyph,     tip(UTF-8),           btn
      {IDM_NEW,          "new",          u8"새 파일",              "Ctrl+N",       L"\xE7C3", u8"새 파일",          BtnType::File},
      {IDM_OPEN,         "open",         u8"열기",                 "Ctrl+O",       L"\xE8E5", u8"열기",             BtnType::File},
      {IDM_SAVE,         "save",         u8"저장",                 "Ctrl+S",       L"\xE74E", u8"저장",             BtnType::File},
      {IDM_SAVEAS,       "saveAs",       u8"다른 이름으로 저장",   "Ctrl+Shift+S", nullptr,   nullptr,              BtnType::None},
      {IDM_VSCODE,       "openInVSCode", u8"VSCode로 열기",        "Ctrl+Shift+V", L"\xE943", u8"VS Code 로 열기",  BtnType::File},
      {IDM_VIEW_E,       "viewEditor",   u8"에디터만 보기",        "Ctrl+1",       L"\xE70F", u8"편집",             BtnType::View},
      {IDM_VIEW_S,       "viewSplit",    u8"분할 보기",            "Ctrl+2",       L"\xE89A", u8"분할 보기",        BtnType::View},
      {IDM_VIEW_P,       "viewPreview",  u8"미리보기만 보기",      "Ctrl+3",       L"\xE890", u8"미리보기",         BtnType::View},
      {IDM_CYCLE,        "cycleView",    u8"보기 순환",            "Ctrl+\\",      nullptr,   nullptr,              BtnType::None},
      {IDM_INSERTTABLE,  "insertTable",  u8"표 삽입",              "Ctrl+T",       L"\xE80A", u8"표 삽입",          BtnType::File},
      {IDM_INSERTTOC,    "insertToc",    u8"목차 삽입",            "Ctrl+Shift+O", L"\xE8A1", u8"목차 삽입",        BtnType::File},
      {IDM_FORMATTABLE,  "formatTable",  u8"표 정렬",              "Ctrl+Shift+F", L"\xE8CB", u8"표 정렬",          BtnType::File},
      {IDM_SETTINGS,     "settings",     u8"설정 열기",            "Ctrl+,",       L"\xE713", u8"설정",             BtnType::File},
      {IDM_ZOOM_IN,      "zoomIn",       u8"확대",                 "Ctrl+=",       nullptr,   nullptr,              BtnType::None},
      {IDM_ZOOM_OUT,     "zoomOut",      u8"축소",                 "Ctrl+-",       nullptr,   nullptr,              BtnType::None},
      {IDM_ZOOM_RESET,   "zoomReset",    u8"배율 초기화",          "Ctrl+0",       nullptr,   nullptr,              BtnType::None},
      // 창 제어: 재바인딩 불가(keymap 미포함), 상단바 버튼으로만 노출(툴팁 없음).
      {IDM_MIN,          nullptr,        nullptr,                  nullptr,        L"\xE921", nullptr,              BtnType::WinCtrl},
      {IDM_MAX,          nullptr,        nullptr,                  nullptr,        L"\xE922", nullptr,              BtnType::WinCtrl},
      {IDM_WCLOSE,       nullptr,        nullptr,                  nullptr,        L"\xE8BB", nullptr,              BtnType::Close},
  };
  // clang-format on
  return t;
}

const CommandInfo* findCommand(int idm) {
  for (const CommandInfo& c : commandTable())
    if (c.idm == idm) return &c;
  return nullptr;
}
