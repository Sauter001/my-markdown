// 단축키 재바인딩 모달 대화상자. 동작 목록을 보여주고, 선택한 동작에 새 키
// 조합을 눌러 지정한다. 확인 시 settings.json 의 keymap 오버라이드 문자열을
// 갱신한다.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

class ShortcutDialog {
 public:
  // 모달 표시. 확인 시 keymapJson(오버라이드 객체)을 갱신하고 true 반환.
  bool show(HWND parent, HFONT uiFont, std::string& keymapJson);

 private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  void refresh();   // specs_ -> 리스트박스 다시 채우기(선택 유지)
  void resetAll();  // 모든 동작을 기본값으로

  std::vector<std::string> specs_;  // 동작별 현재 스펙(keymap::actions() 순서)
  HWND lb_ = nullptr;
  bool done_ = false;
  bool ok_ = false;
};
