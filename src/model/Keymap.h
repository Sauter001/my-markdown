// 단축키 매핑(헤더 온리). 동작 목록 + 기본 바인딩, 스펙 문자열("Ctrl+Shift+S")
// 파싱/포맷, ACCEL 테이블 빌드, preview.js 동기화용 JSON 생성.
//   사용자 오버라이드는 settings.json 의 "keymap" 객체({id: spec})로 저장한다.
#pragma once
#include <windows.h>

#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "commands.h"
#include "core/json.h"

namespace keymap {

struct Action {
  const char* id;     // 안정 식별자(설정/브리지 공용)
  int idm;            // WM_COMMAND 명령 ID
  const char* label;  // UTF-8 한글 라벨(설정 UI)
  const char* def;    // 기본 스펙
};

// 재바인딩 가능한 동작 목록(고정 순서). preview.js 키 처리와 동기화된다.
inline const std::vector<Action>& actions() {
  static const std::vector<Action> a = {
      {"new",          IDM_NEW,         u8"새 파일",            "Ctrl+N"},
      {"open",         IDM_OPEN,        u8"열기",               "Ctrl+O"},
      {"save",         IDM_SAVE,        u8"저장",               "Ctrl+S"},
      {"saveAs",       IDM_SAVEAS,      u8"다른 이름으로 저장", "Ctrl+Shift+S"},
      {"openInVSCode", IDM_VSCODE,      u8"VSCode로 열기",      "Ctrl+Shift+V"},
      {"viewEditor",   IDM_VIEW_E,      u8"에디터만 보기",      "Ctrl+1"},
      {"viewSplit",    IDM_VIEW_S,      u8"분할 보기",          "Ctrl+2"},
      {"viewPreview",  IDM_VIEW_P,      u8"미리보기만 보기",    "Ctrl+3"},
      {"cycleView",    IDM_CYCLE,       u8"보기 순환",          "Ctrl+\\"},
      {"insertTable",  IDM_INSERTTABLE, u8"표 삽입",            "Ctrl+T"},
      {"insertToc",    IDM_INSERTTOC,   u8"목차 삽입",          "Ctrl+Shift+O"},
      {"formatTable",  IDM_FORMATTABLE, u8"표 정렬",            "Ctrl+Shift+F"},
      {"settings",     IDM_SETTINGS,    u8"설정 열기",          "Ctrl+,"},
      {"zoomIn",       IDM_ZOOM_IN,     u8"확대",               "Ctrl+="},
      {"zoomOut",      IDM_ZOOM_OUT,    u8"축소",               "Ctrl+-"},
      {"zoomReset",    IDM_ZOOM_RESET,  u8"배율 초기화",        "Ctrl+0"},
  };
  return a;
}

// 안정 id(예: "save") -> WM_COMMAND 명령 ID(IDM_*). 없으면 0.
inline int idmForId(const std::string& id) {
  for (const Action& a : actions())
    if (id == a.id) return a.idm;
  return 0;
}

inline bool iequals(const std::string& a, const char* b) {
  size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; i++)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
      return false;
  return true;
}

// 키 토큰(예: "S","1",",","\\","=") -> 가상키. 실패 시 0.
inline WORD vkFromToken(const std::string& t) {
  if (t.size() == 1) {
    char c = t[0];
    if (c >= 'a' && c <= 'z') return (WORD)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return (WORD)c;
    if (c >= '0' && c <= '9') return (WORD)c;
    switch (c) {
      case ',': return VK_OEM_COMMA;
      case '.': return VK_OEM_PERIOD;
      case '\\': return VK_OEM_5;
      case '=': return VK_OEM_PLUS;
      case '-': return VK_OEM_MINUS;
      case ';': return VK_OEM_1;
      case '/': return VK_OEM_2;
      case '`': return VK_OEM_3;
      case '[': return VK_OEM_4;
      case ']': return VK_OEM_6;
      case '\'': return VK_OEM_7;
    }
  }
  return 0;
}

// 가상키 -> 키 토큰. 실패 시 빈 문자열.
inline std::string tokenFromVk(WORD vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    return std::string(1, (char)vk);
  switch (vk) {
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_5: return "\\";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_1: return ";";
    case VK_OEM_2: return "/";
    case VK_OEM_3: return "`";
    case VK_OEM_4: return "[";
    case VK_OEM_6: return "]";
    case VK_OEM_7: return "'";
  }
  return "";
}

// "Ctrl+Shift+S" -> fVirt/key (ACCEL). 성공 시 true.
inline bool parseSpec(const std::string& spec, BYTE& fVirt, WORD& key) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : spec) {
    if (c == '+') { parts.push_back(cur); cur.clear(); }
    else cur += c;
  }
  parts.push_back(cur);
  if (parts.empty() || parts.back().empty()) return false;
  BYTE f = FVIRTKEY;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    if (iequals(parts[i], "ctrl")) f |= FCONTROL;
    else if (iequals(parts[i], "shift")) f |= FSHIFT;
    else if (iequals(parts[i], "alt")) f |= FALT;
    else return false;
  }
  WORD vk = vkFromToken(parts.back());
  if (!vk) return false;
  fVirt = f;
  key = vk;
  return true;
}

// 수식어 + 가상키 -> 스펙 문자열(캡처 UI). 실패 시 빈 문자열.
inline std::string formatSpec(bool ctrl, bool shift, bool alt, WORD vk) {
  std::string tok = tokenFromVk(vk);
  if (tok.empty()) return "";
  std::string s;
  if (ctrl) s += "Ctrl+";
  if (shift) s += "Shift+";
  if (alt) s += "Alt+";
  s += tok;
  return s;
}

// 해당 동작의 현재 스펙(오버라이드 우선, 없으면 기본).
// jsonStr 은 이스케이프를 풀지 않으므로, 스펙에 나올 수 있는 유일한 특수문자인
// 백슬래시(`\\` -> `\`)만 보정한다(기본값은 이스케이프되지 않아 영향 없음).
inline std::string currentSpec(const Action& a, const std::string& keymapJson) {
  std::string s = jsonStr(keymapJson, a.id, a.def);
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) { o += s[i + 1]; i++; }
    else o += s[i];
  }
  return o;
}

// 스펙의 키 토큰을 소문자로(preview.js 의 e.key.toLowerCase() 비교용).
inline std::string jsKey(const std::string& spec) {
  size_t plus = spec.find_last_of('+');
  std::string tok = (plus == std::string::npos) ? spec : spec.substr(plus + 1);
  for (char& c : tok) c = (char)std::tolower((unsigned char)c);
  return tok;
}

// 에디터 포커스용 ACCEL 테이블. 넘버패드 줌 보조키는 고정으로 덧붙인다.
inline std::vector<ACCEL> buildAccels(const std::string& keymapJson) {
  std::vector<ACCEL> v;
  for (const Action& a : actions()) {
    BYTE f;
    WORD vk;
    if (parseSpec(currentSpec(a, keymapJson), f, vk))
      v.push_back({f, vk, (WORD)a.idm});
  }
  v.push_back({FVIRTKEY | FCONTROL, VK_ADD, (WORD)IDM_ZOOM_IN});
  v.push_back({FVIRTKEY | FCONTROL, VK_SUBTRACT, (WORD)IDM_ZOOM_OUT});
  v.push_back({FVIRTKEY | FCONTROL, VK_NUMPAD0, (WORD)IDM_ZOOM_RESET});
  return v;
}

// preview.js 동기화용 JSON 배열: [{"id","ctrl","shift","alt","key"}].
inline std::string previewJson(const std::string& keymapJson) {
  std::string o = "[";
  bool first = true;
  for (const Action& a : actions()) {
    std::string spec = currentSpec(a, keymapJson);
    BYTE f;
    WORD vk;
    if (!parseSpec(spec, f, vk)) continue;
    auto bs = [](bool b) { return b ? "true" : "false"; };
    if (!first) o += ",";
    first = false;
    o += "{\"id\":\"" + std::string(a.id) + "\",\"ctrl\":" + bs((f & FCONTROL) != 0) +
         ",\"shift\":" + bs((f & FSHIFT) != 0) + ",\"alt\":" + bs((f & FALT) != 0) +
         ",\"key\":\"" + jsonEscape(jsKey(spec)) + "\"}";
  }
  o += "]";
  return o;
}

// 동작 id->스펙 쌍을 settings.json 의 keymap 객체 문자열로 직렬화.
inline std::string toJson(const std::vector<std::pair<std::string, std::string>>& binds) {
  std::string o = "{";
  bool first = true;
  for (const auto& b : binds) {
    if (!first) o += ",";
    first = false;
    o += "\"" + b.first + "\":\"" + jsonEscape(b.second) + "\"";
  }
  o += "}";
  return o;
}

}  // namespace keymap
