#include "model/Settings.h"

#include <string>

#include "core/file_io.h"
#include "core/json.h"

void Settings::load() {
  std::string j;
  if (!readFile(settingsPath(), j)) return;  // 없으면 기본값 유지
  fontSize = jsonInt(j, "fontSize", fontSize);
  tabSize = jsonInt(j, "tabSize", tabSize);
  wrap = jsonBool(j, "wrap", wrap);
  theme = jsonStr(j, "theme", theme);
  defaultView = jsonStr(j, "defaultView", defaultView);
  scrollSync = jsonBool(j, "scrollSync", scrollSync);
  smoothScroll = jsonBool(j, "smoothScroll", smoothScroll);
  scrollLines = jsonInt(j, "scrollLines", scrollLines);
  autoPair = jsonBool(j, "autoPair", autoPair);
  langsJson = jsonArrayRaw(j, "highlightLanguages", langsJson);
  keymapJson = jsonObjectRaw(j, "keymap", keymapJson);
  zoom = jsonInt(j, "zoom", zoom);
  fontSize = clampRange(fontSize, 6, 40);  // pt
  tabSize = clampRange(tabSize, 1, 8);
  zoom = clampRange(zoom, 50, 300);
  scrollLines = clampRange(scrollLines, 1, 15);
}

// 직렬화. 읽기 파서(jsonStr/jsonInt/jsonBool/jsonArrayRaw)와 호환되는 평면
// JSON. highlightLanguages 는 langsJson 이 이미 유효한 배열 리터럴이라 그대로
// 삽입.
bool Settings::save() const {
  std::string j = "{\n";
  j += "  \"defaultView\": \"" + jsonEscape(defaultView) + "\",\n";
  j += "  \"theme\": \"" + jsonEscape(theme) + "\",\n";
  j += "  \"fontSize\": " + std::to_string(fontSize) + ",\n";
  j += "  \"tabSize\": " + std::to_string(tabSize) + ",\n";
  j += "  \"wrap\": " + std::string(wrap ? "true" : "false") + ",\n";
  j +=
      "  \"scrollSync\": " + std::string(scrollSync ? "true" : "false") + ",\n";
  j += "  \"smoothScroll\": " + std::string(smoothScroll ? "true" : "false") +
       ",\n";
  j += "  \"scrollLines\": " + std::to_string(scrollLines) + ",\n";
  j += "  \"autoPair\": " + std::string(autoPair ? "true" : "false") + ",\n";
  j += "  \"zoom\": " + std::to_string(zoom) + ",\n";
  j += "  \"keymap\": " + keymapJson + ",\n";
  j += "  \"highlightLanguages\": " + langsJson + "\n";
  j += "}\n";
  return writeFile(settingsPath(), j);
}
