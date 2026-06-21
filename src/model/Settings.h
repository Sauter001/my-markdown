// 앱 설정 모델 (settings.json 로드/저장 + 검증).
#pragma once
#include <string>

struct Settings {
  int fontSize = 10;  // 글꼴 크기(pt). 실제 픽셀은 DPI/줌 반영해 계산
  int tabSize = 4;
  bool wrap = true;
  std::string theme = "system";       // system/light/dark
  std::string defaultView = "split";  // editor/split/preview
  bool scrollSync = true;
  bool autoPair = true;  // 괄호/따옴표/백틱/별표 자동 페어링
  std::string langsJson =
      "[\"bash\",\"c\",\"cpp\",\"java\",\"python\",\"html\",\"css\","
      "\"javascript\",\"sql\",\"json\"]";
  int zoom = 100;  // 글자 배율(%)
  std::string keymapJson = "{}";  // 단축키 오버라이드 객체({id: spec}), 기본은 빈 객체

  void load();  // settingsPath() 에서 읽고 범위 클램프 (없으면 기본값 유지)
  bool save() const;  // 읽기 파서와 호환되는 평면 JSON 으로 직렬화
  static int clampRange(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
};
