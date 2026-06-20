// 네이티브 EDIT 컨트롤 소유 + 서브클래스 동작(Tab 들여쓰기, 리스트 자동
// 이어쓰기, 스크롤 통지) + 글꼴/탭/줄바꿈 스타일 적용. 표 처리는 콜백으로 App
// 에 위임.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include "core/raii.h"

struct Settings;

class Editor {
 public:
  void create(HWND parent, const Settings& settings);
  void recreateForWrap(
      HWND parent);  // wrap 토글: 본문/선택 보존하며 컨트롤 재생성
  HWND hwnd() const { return edit_; }

  std::wstring getTextW() const;                  // EDIT 본문(wide/CRLF)
  std::string getTextUtf8Lf() const;              // 파일용 UTF-8/LF
  void setTextUtf8Lf(const std::string& utf8lf);  // 프로그램적 설정(더티 억제)
  void applyStyle();  // 글꼴/탭폭/마진 (설정/줌 반영)
  bool isSuppressing() const {
    return suppress_;
  }  // EN_CHANGE 가 더티로 잡히면 안 되는 구간

  // 에디터 동작
  void tabIndent(bool shift);
  bool listEnter();

  // 콜백 (App 가 배선)
  std::function<bool(bool shift)> onTableNav;  // 표에서 처리하면 true
  std::function<bool()> onTableEnter;          // 표에서 처리하면 true
  std::function<void()> onScroll;              // 스크롤 시 통지(프리뷰 동기화)

 private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT onMessage(HWND, UINT, WPARAM, LPARAM);
  void blockIndent(const std::wstring& text, DWORD a, DWORD b, bool shift);

  HWND edit_ = nullptr;
  WNDPROC orig_ = nullptr;
  FontHandle font_;
  bool suppress_ = false;     // 프로그램적 본문 변경 중(더티 무시)
  bool swallowChar_ = false;  // Tab/Enter 처리 후 뒤따르는 WM_CHAR 삼킴
  const Settings* settings_ = nullptr;  // 비소유, App 소유 Settings 참조
};
