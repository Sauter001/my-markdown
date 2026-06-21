// 설정 모달 대화상자. 값만 편집해 Settings 에 반영하고 저장 여부를 반환한다.
//   런타임 적용(글꼴/줄바꿈/테마/프리뷰)은 호출자(App)가 전후 비교로 수행한다.
#pragma once
#include <windows.h>

#include <string>

struct Settings;

class SettingsDialog {
 public:
  // 저장 시 settings 를 갱신하고 true 반환(취소 시 settings 불변, false).
  bool show(HWND parent, HFONT uiFont, Settings& settings);

 private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  HFONT uiFont_ = nullptr;  // 하위 대화상자(단축키)에 전달
  std::string keymapWork_;  // 단축키 작업본(저장 시 settings 로 반영)
  bool done_ = false;
  bool ok_ = false;
};
