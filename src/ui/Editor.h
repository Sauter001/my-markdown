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
  void applyStyle();  // 글꼴/탭폭/마진/행간 (설정/줌 반영)
  void applyColors(COLORREF bg, COLORREF fg);  // RichEdit 배경/글자색 (테마)
  bool isSuppressing() const {
    return suppress_;
  }  // EN_CHANGE 가 더티로 잡히면 안 되는 구간

  // 에디터 동작
  void tabIndent(bool shift);
  bool listEnter();
  void insertToc();  // 헤더 스캔 -> 목차(중첩 링크 목록) 캐럿에 삽입

  // 콜백 (App 가 배선)
  std::function<bool(bool shift)> onTableNav;  // 표에서 처리하면 true
  std::function<bool()> onTableEnter;          // 표에서 처리하면 true
  std::function<void()> onScroll;              // 스크롤 시 통지(프리뷰 동기화)

 private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT onMessage(HWND, UINT, WPARAM, LPARAM);
  void applyLineSpacing();  // 줄 간격(행간) 적용: 가독성 위해 단행보다 넓게
  void blockIndent(const std::wstring& text, DWORD a, DWORD b, bool shift);
  bool autoPair(wchar_t c);  // 괄호/따옴표/백틱/별표 입력 시 짝 처리(처리하면 true)
  bool pairBackspace();      // 빈 짝 사이 Backspace 시 양쪽 삭제(처리하면 true)
  bool autoIndentEnter();    // Enter 시 들여쓰기 유지(코드블록 중괄호는 한 단계 증가)
  bool inCodeBlock(const std::wstring& text, DWORD pos);  // pos 가 펜스 코드블록 내부인지
  void lineRange(DWORD& start, DWORD& end);  // 선택/캐럿이 걸친 라인 범위(개행 포함)
  void copyLine();    // 현재 라인 복사(선택 없을 때 Ctrl+C)
  void cutLine();     // 현재 라인 잘라내기(선택 없을 때 Ctrl+X)
  void deleteLine();  // 현재 라인 삭제(Ctrl+Del)

  // 부드러운 휠 스크롤: 휠 입력을 목표 픽셀 위치로 누적하고 타이머로 이징 이동.
  void wheelScroll(int wheelDelta);  // 휠 1회(부호 있는 WHEEL_DELTA 배수) 처리
  void smoothScrollTick();           // 애니메이션 1프레임
  void stopSmoothScroll();           // 애니메이션 중단(직접 조작 시)
  int lineHeightPx() const;          // 현재 글꼴 기준 한 줄 픽셀 높이

  HWND edit_ = nullptr;
  WNDPROC orig_ = nullptr;
  FontHandle font_;
  bool suppress_ = false;     // 프로그램적 본문 변경 중(더티 무시)
  bool swallowChar_ = false;  // Tab/Enter 처리 후 뒤따르는 WM_CHAR 삼킴
  COLORREF bg_ = RGB(255, 255, 255);  // 마지막 적용 배경색(재생성 시 복원)
  COLORREF fg_ = RGB(0, 0, 0);        // 마지막 적용 글자색
  const Settings* settings_ = nullptr;  // 비소유, App 소유 Settings 참조

  bool smoothActive_ = false;   // 휠 애니메이션 진행 중(타이머 동작 여부)
  double smoothCurY_ = 0.0;     // 현재 스크롤 픽셀 위치(소수 누적으로 부드럽게)
  double smoothTargetY_ = 0.0;  // 목표 스크롤 픽셀 위치
};
