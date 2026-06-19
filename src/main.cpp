// MyMD - 간이 마크다운 에디터 (네이티브 Win32)
//
// Phase 2: 에디터를 WebView 밖 네이티브 EDIT 컨트롤로 분리한다(메모장급 즉시 로딩).
// 이 단계(2a)는 네이티브 셸 + 네이티브 에디터만 담당한다(프리뷰 없음).
//   - 네이티브 창 + 멀티라인 EDIT 컨트롤
//   - 파일 새로/열기/저장/다른이름저장 (네이티브 대화상자 + 입출력)
//   - 더티/제목, 설정(글꼴/탭/줄바꿈/테마) 적용, 단축키, 닫기 확인
// 프리뷰(WebView2)는 2c에서 우측 패널에 지연 임베드한다.

#include <windows.h>
#include <windowsx.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include "webview.h"
#include "resource.h"

// 명령 ID (단축키/메뉴)
#define IDM_NEW    101
#define IDM_OPEN   102
#define IDM_SAVE   103
#define IDM_SAVEAS 104
#define IDM_VSCODE 105
#define IDM_INSERTTABLE 106
#define IDM_FORMATTABLE 107
#define IDM_SETTINGS    108
#define IDT_RENDER 1   // 프리뷰 디바운스 타이머
#define IDT_SCROLLSYNC 2 // 스크롤 동기화 디바운스 타이머

// ---------------------------------------------------------------------------
// 전역 상태
// ---------------------------------------------------------------------------
static std::wstring g_curPath;     // 현재 파일 전체 경로(비어 있으면 제목 없음)
static std::wstring g_curName;     // 제목 표시용 파일명
static bool         g_dirty = false;
static HWND         g_hwnd  = nullptr;  // 메인 창
static HWND         g_edit  = nullptr;  // 에디터 EDIT 컨트롤
static HFONT        g_editFont = nullptr;
static HBRUSH       g_editBrush = nullptr;
static std::wstring g_pendingOpen;      // 실행 인자로 전달된 파일
static bool         g_suppressDirty = false; // 프로그램적 본문 설정 시 더티 무시

// 설정 (settings.json)
static int          g_fontSize = 14;
static int          g_tabSize  = 4;
static bool         g_wrap     = true;
static std::string  g_theme    = "system"; // system/light/dark
static std::string  g_defaultView = "split";
static bool         g_scrollSync = true;
static std::string  g_langsJson = "[\"bash\",\"c\",\"cpp\",\"java\",\"python\",\"html\",\"css\",\"javascript\",\"sql\",\"json\"]";
static std::wstring g_curDir;   // 현재 문서 폴더 (이미지 base)

// 프리뷰 (WebView2, 지연 생성)
static HWND g_preview = nullptr;                  // 프리뷰 호스트 자식 창
static webview::webview *g_webview = nullptr;     // WebView2 엔진 (split/preview 진입 시 생성)
static bool g_webviewReady = false;               // preview.js 준비 완료
static int g_view = 1;            // 0 에디터, 1 분할, 2 미리보기
static double g_splitRatio = 0.5; // 분할 보기 에디터 비율
static bool g_divDrag = false;    // 디바이더 드래그 중
static int g_dividerX = -1;       // 디바이더 좌표 (분할 보기, 아니면 -1)
static const int kDividerW = 5;

static void refreshPreview(); // 전방 선언 (정의는 프리뷰 섹션)

// ---------------------------------------------------------------------------
// 문자열 변환 (UTF-8 <-> UTF-16)
// ---------------------------------------------------------------------------
static std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
static std::string wide_to_utf8(const std::wstring &w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}
// 소스의 UTF-8 좁은 리터럴을 와이드로 (한글 UI 문자열용)
static std::wstring W(const char *utf8) { return utf8_to_wide(utf8); }

// ---------------------------------------------------------------------------
// base64 (네이티브 <-> 프리뷰 브리지용)
// ---------------------------------------------------------------------------
static const char *B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64_encode(const std::string &in) {
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    unsigned a = (unsigned char)in[i], b = (unsigned char)in[i + 1], c = (unsigned char)in[i + 2];
    unsigned n = (a << 16) | (b << 8) | c;
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];  out += B64[n & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    unsigned n = (unsigned char)in[i] << 16;
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63]; out += '='; out += '=';
  } else if (i + 2 == in.size()) {
    unsigned n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63]; out += B64[(n >> 6) & 63]; out += '=';
  }
  return out;
}
static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
static std::string base64_decode(const std::string &in) {
  std::string out; out.reserve((in.size() / 4) * 3);
  int buf = 0, bits = 0;
  for (char c : in) {
    if (c == '=') break;
    int v = b64val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}
// 브리지 인자(base64 문자열)에서 첫 따옴표 문자열 추출
static std::string firstStringArg(const std::string &req) {
  size_t a = req.find('"');
  if (a == std::string::npos) return std::string();
  size_t b = req.find('"', a + 1);
  if (b == std::string::npos) return std::string();
  return req.substr(a + 1, b - a - 1);
}
// file:/// URL 생성 (UTF-8 퍼센트 인코딩)
static std::string toFileUrl(const std::wstring &path) {
  std::string utf8 = wide_to_utf8(path);
  std::string out = "file:///";
  for (unsigned char c : utf8) {
    if (c == '\\') { out += '/'; continue; }
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~' || c == '/' || c == ':';
    if (keep) out += (char)c;
    else { char b[4]; sprintf(b, "%%%02X", c); out += b; }
  }
  return out;
}

// ---------------------------------------------------------------------------
// 줄바꿈 정규화 (EDIT 는 CRLF, 파일은 LF 로 유지)
// ---------------------------------------------------------------------------
static std::string crlfToLF(const std::string &s) {
  std::string o; o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') { o += '\n'; if (i + 1 < s.size() && s[i + 1] == '\n') i++; }
    else o += s[i];
  }
  return o;
}
static std::string lfToCRLF(const std::string &s) {
  std::string o; o.reserve(s.size() + 16);
  for (char c : s) { if (c == '\n') o += "\r\n"; else o += c; }
  return o;
}

// ---------------------------------------------------------------------------
// 파일 입출력 (와이드 경로)
// ---------------------------------------------------------------------------
static bool readFile(const std::wstring &path, std::string &out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
  out.resize((size_t)sz.QuadPart);
  size_t total = 0;
  bool ok = true;
  while (total < out.size()) {
    DWORD toRead = (DWORD)std::min(out.size() - total, (size_t)(1 << 20));
    DWORD rd = 0;
    if (!ReadFile(h, &out[total], toRead, &rd, nullptr)) { ok = false; break; }
    if (rd == 0) break;
    total += rd;
  }
  CloseHandle(h);
  if (ok) out.resize(total);
  // UTF-8 BOM 제거
  if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
      (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
    out.erase(0, 3);
  return ok;
}

static bool writeFile(const std::wstring &path, const std::string &bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  size_t total = 0;
  bool ok = true;
  while (total < bytes.size()) {
    DWORD toW = (DWORD)std::min(bytes.size() - total, (size_t)(1 << 20));
    DWORD wr = 0;
    if (!WriteFile(h, bytes.data() + total, toW, &wr, nullptr)) { ok = false; break; }
    total += wr;
  }
  CloseHandle(h);
  return ok;
}

// ---------------------------------------------------------------------------
// 경로 도우미
// ---------------------------------------------------------------------------
static std::wstring baseName(const std::wstring &p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? p : p.substr(i + 1);
}
static std::wstring exeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
static std::wstring settingsPath() { return exeDir() + L"\\settings.json"; }

static std::wstring dirOf(const std::wstring &p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
static void setCurrentFile(const std::wstring &path) {
  g_curPath = path;
  g_curName = baseName(path);
  g_curDir  = dirOf(path);
}

// ---------------------------------------------------------------------------
// 설정 파싱 (평면 필드용 최소 파서)
// ---------------------------------------------------------------------------
static size_t jsonValuePos(const std::string &j, const char *key) {
  std::string pat = std::string("\"") + key + "\"";
  size_t k = j.find(pat);
  if (k == std::string::npos) return std::string::npos;
  size_t c = j.find(':', k + pat.size());
  if (c == std::string::npos) return std::string::npos;
  size_t p = c + 1;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) p++;
  return p;
}
static std::string jsonStr(const std::string &j, const char *key, const std::string &dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '"') return dft;
  size_t q = j.find('"', p + 1);
  if (q == std::string::npos) return dft;
  return j.substr(p + 1, q - p - 1);
}
static int jsonInt(const std::string &j, const char *key, int dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size()) return dft;
  int sign = 1;
  if (j[p] == '-') { sign = -1; p++; }
  if (p >= j.size() || !isdigit((unsigned char)j[p])) return dft;
  long v = 0;
  while (p < j.size() && isdigit((unsigned char)j[p])) { v = v * 10 + (j[p] - '0'); p++; }
  return (int)(sign * v);
}
static bool jsonBool(const std::string &j, const char *key, bool dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos) return dft;
  if (j.compare(p, 4, "true") == 0) return true;
  if (j.compare(p, 5, "false") == 0) return false;
  return dft;
}
static std::string jsonArrayRaw(const std::string &j, const char *key, const std::string &dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '[') return dft;
  int depth = 0;
  for (size_t i = p; i < j.size(); i++) {
    if (j[i] == '[') depth++;
    else if (j[i] == ']') { if (--depth == 0) return j.substr(p, i - p + 1); }
  }
  return dft;
}
static void loadSettings() {
  std::string j;
  if (!readFile(settingsPath(), j)) return; // 없으면 기본값 유지
  g_fontSize    = jsonInt(j, "fontSize", g_fontSize);
  g_tabSize     = jsonInt(j, "tabSize", g_tabSize);
  g_wrap        = jsonBool(j, "wrap", g_wrap);
  g_theme       = jsonStr(j, "theme", g_theme);
  g_defaultView = jsonStr(j, "defaultView", g_defaultView);
  g_scrollSync  = jsonBool(j, "scrollSync", g_scrollSync);
  g_langsJson   = jsonArrayRaw(j, "highlightLanguages", g_langsJson);
  if (g_fontSize < 10) g_fontSize = 10;
  if (g_fontSize > 32) g_fontSize = 32;
  if (g_tabSize < 1) g_tabSize = 1;
  if (g_tabSize > 8) g_tabSize = 8;
}

// 설정 직렬화. 읽기 파서(jsonStr/jsonInt/jsonBool/jsonArrayRaw)와 호환되는 평면 JSON.
// highlightLanguages 는 g_langsJson 이 이미 유효한 배열 리터럴이라 그대로 삽입.
static std::string jsonEscape(const std::string &s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", (unsigned)(unsigned char)c); o += b; }
        else o += c;
    }
  }
  return o;
}
static bool saveSettings() {
  std::string j = "{\n";
  j += "  \"defaultView\": \"" + jsonEscape(g_defaultView) + "\",\n";
  j += "  \"theme\": \"" + jsonEscape(g_theme) + "\",\n";
  j += "  \"fontSize\": " + std::to_string(g_fontSize) + ",\n";
  j += "  \"tabSize\": " + std::to_string(g_tabSize) + ",\n";
  j += "  \"wrap\": " + std::string(g_wrap ? "true" : "false") + ",\n";
  j += "  \"scrollSync\": " + std::string(g_scrollSync ? "true" : "false") + ",\n";
  j += "  \"highlightLanguages\": " + g_langsJson + "\n";
  j += "}\n";
  return writeFile(settingsPath(), j);
}

// ---------------------------------------------------------------------------
// 테마
// ---------------------------------------------------------------------------
static bool isDarkTheme() {
  if (g_theme == "dark") return true;
  if (g_theme == "light") return false;
  DWORD val = 1, sz = sizeof(val); // system: 레지스트리 AppsUseLightTheme(0=다크)
  HKEY hk;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &hk) == ERROR_SUCCESS) {
    RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val, &sz);
    RegCloseKey(hk);
  }
  return val == 0;
}
static COLORREF themeBg() { return isDarkTheme() ? RGB(0x0d, 0x11, 0x17) : RGB(0xff, 0xff, 0xff); }
static COLORREF themeFg() { return isDarkTheme() ? RGB(0xe6, 0xed, 0xf3) : RGB(0x1f, 0x23, 0x28); }

static HFONT makeEditFont(int px) {
  return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     FIXED_PITCH | FF_MODERN, L"Consolas");
}

// 상단바 테마색
static COLORREF themeTopbarBg() { return isDarkTheme() ? RGB(0x16, 0x1b, 0x22) : RGB(0xf6, 0xf8, 0xfa); }
static COLORREF themeBorder()   { return isDarkTheme() ? RGB(0x30, 0x36, 0x3d) : RGB(0xe1, 0xe4, 0xe8); }
static COLORREF themeBtnHover() { return isDarkTheme() ? RGB(0x30, 0x36, 0x3d) : RGB(0xee, 0xf1, 0xf4); }
static COLORREF themeMuted()    { return isDarkTheme() ? RGB(0x8b, 0x94, 0x9e) : RGB(0x6e, 0x77, 0x81); }
static COLORREF themeAccent()   { return isDarkTheme() ? RGB(0x2f, 0x81, 0xf7) : RGB(0x09, 0x69, 0xda); }

// ---------------------------------------------------------------------------
// 네이티브 상단바 (프레임리스 창의 커스텀 타이틀바/툴바)
// ---------------------------------------------------------------------------
static const int kTopbarH = 38;
static const int kResizeBorder = 6;
static HFONT g_uiFont = nullptr;
static HFONT g_glyphFont = nullptr; // 창 제어 아이콘 (Segoe MDL2 Assets)

enum { IDM_MIN = 201, IDM_MAX = 202, IDM_WCLOSE = 203,
       IDM_VIEW_E = 301, IDM_VIEW_S = 302, IDM_VIEW_P = 303 };
struct TopBtn { int id; std::wstring label; RECT rc; int type; }; // type: 0 텍스트, 1 창제어, 2 닫기, 3 보기세그
static std::vector<TopBtn> g_btns;
static int g_hotBtn = -1;

static void invalidateTopbar() {
  if (!g_hwnd) return;
  RECT cr; GetClientRect(g_hwnd, &cr);
  RECT bar = { 0, 0, cr.right, kTopbarH };
  InvalidateRect(g_hwnd, &bar, FALSE);
}

// ---------------------------------------------------------------------------
// 창 제목 갱신
// ---------------------------------------------------------------------------
static void updateTitle() {
  std::wstring t;
  if (g_dirty) t += L"* ";
  t += g_curName.empty() ? W(u8"제목 없음") : g_curName;
  if (g_hwnd) SetWindowTextW(g_hwnd, t.c_str());
  invalidateTopbar(); // 파일명/더티 점 갱신
}

// ---------------------------------------------------------------------------
// 에디터 본문 입출력
// ---------------------------------------------------------------------------
static std::string getEditText() {
  int len = GetWindowTextLengthW(g_edit);
  std::wstring w;
  w.resize(len + 1);
  int got = GetWindowTextW(g_edit, &w[0], len + 1);
  w.resize(got);
  return crlfToLF(wide_to_utf8(w)); // 파일에는 LF 로 저장
}
static void setEditText(const std::string &utf8_lf) {
  std::wstring w = utf8_to_wide(lfToCRLF(utf8_lf)); // EDIT 에는 CRLF 로
  g_suppressDirty = true;
  SetWindowTextW(g_edit, w.c_str());
  g_suppressDirty = false;
}

// ---------------------------------------------------------------------------
// 파일 대화상자
// ---------------------------------------------------------------------------
static bool openDialog(std::wstring &outPath) {
  wchar_t buf[4096] = {0};
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hwnd;
  ofn.lpstrFilter =
      L"Markdown (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.mdown;*.txt\0"
      L"All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn)) { outPath = buf; return true; }
  return false;
}
static bool saveDialog(const std::wstring &suggested, std::wstring &outPath) {
  wchar_t buf[4096] = {0};
  if (!suggested.empty()) wcsncpy(buf, suggested.c_str(), 4095);
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hwnd;
  ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.lpstrDefExt = L"md";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&ofn)) { outPath = buf; return true; }
  return false;
}

// ---------------------------------------------------------------------------
// 파일 동작
// ---------------------------------------------------------------------------
static bool confirmDiscard() {
  if (!g_dirty) return true;
  int r = MessageBoxW(g_hwnd,
                      W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x80 \xEB\xB3\x80\xEA\xB2\xBD\xEC\x82\xAC\xED\x95\xAD\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. \xEA\xB3\x84\xEC\x86\x8D\xED\x95\x98\xEC\x8B\x9C\xEA\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?").c_str(),
                      L"MyMD", MB_YESNO | MB_ICONWARNING);
  return r == IDYES;
}

static bool doSaveAs() {
  std::wstring suggested = g_curName.empty() ? L"untitled.md" : g_curName;
  std::wstring path;
  if (!saveDialog(suggested, path)) return false;
  if (!writeFile(path, getEditText())) {
    MessageBoxW(g_hwnd, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  setCurrentFile(path);
  g_dirty = false;
  updateTitle();
  refreshPreview(); // 경로(이미지 base) 변경 반영
  return true;
}
static bool doSave() {
  if (g_curPath.empty()) return doSaveAs();
  if (!writeFile(g_curPath, getEditText())) {
    MessageBoxW(g_hwnd, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  g_dirty = false;
  updateTitle();
  return true;
}
static void doNew() {
  if (!confirmDiscard()) return;
  g_curPath.clear();
  g_curName.clear();
  g_curDir.clear();
  setEditText("");
  g_dirty = false;
  updateTitle();
  refreshPreview();
  SetFocus(g_edit);
}
static void doOpen() {
  if (!confirmDiscard()) return;
  std::wstring path;
  if (!openDialog(path)) return;
  std::string content;
  if (!readFile(path, content)) {
    MessageBoxW(g_hwnd, L"read failed", L"MyMD", MB_OK | MB_ICONERROR);
    return;
  }
  setCurrentFile(path);
  setEditText(content);
  g_dirty = false;
  updateTitle();
  refreshPreview();
  SetFocus(g_edit);
}

// 설치된 Code.exe를 우선 찾아 파일 인자로 실행하고,
// 못 찾으면 PATH의 code(code.cmd)로 창 없이 폴백한다.
static bool launchVSCode(const std::wstring &file) {
  auto envp = [](const wchar_t *name) -> std::wstring {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
  };
  auto tryExe = [&](const std::wstring &exe) -> bool {
    if (exe.empty()) return false;
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    std::wstring params = L"\"" + file + L"\"";
    return (INT_PTR)ShellExecuteW(g_hwnd, L"open", exe.c_str(),
                                  params.c_str(), nullptr, SW_SHOWNORMAL) > 32;
  };
  std::wstring local = envp(L"LOCALAPPDATA");
  std::wstring pf    = envp(L"ProgramFiles");
  std::wstring pf86  = envp(L"ProgramFiles(x86)");
  if (!local.empty() && tryExe(local + L"\\Programs\\Microsoft VS Code\\Code.exe")) return true;
  if (!pf.empty()    && tryExe(pf    + L"\\Microsoft VS Code\\Code.exe")) return true;
  if (!pf86.empty()  && tryExe(pf86  + L"\\Microsoft VS Code\\Code.exe")) return true;
  // 폴백: PATH의 code(code.cmd)를 콘솔 창 없이 실행
  std::wstring cmd = L"cmd.exe /c code \"" + file + L"\"";
  std::vector<wchar_t> cbuf(cmd.begin(), cmd.end());
  cbuf.push_back(L'\0');
  STARTUPINFOW si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cbuf.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  }
  return false;
}
// 현재 문서를 저장한 뒤 VSCode로 연다(제목 없으면 다른 이름으로 저장).
static void doOpenInVSCode() {
  if (g_dirty || g_curPath.empty()) {
    if (!doSave()) return; // 저장 취소/실패 시 중단
  }
  if (g_curPath.empty()) return;
  if (!launchVSCode(g_curPath)) {
    MessageBoxW(g_hwnd,
                W(u8"VSCode(Code.exe)를 찾을 수 없습니다. 설치 여부나 PATH를 확인하세요.").c_str(),
                L"MyMD", MB_OK | MB_ICONERROR);
  }
}

// ---------------------------------------------------------------------------
// 상단바 레이아웃/그리기/히트테스트
// ---------------------------------------------------------------------------
static int textW(HDC dc, const std::wstring &s) {
  SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx;
}
static void layoutTopbar(int width) {
  g_btns.clear();
  HDC dc = GetDC(g_hwnd);
  HFONT old = (HFONT)SelectObject(dc, g_uiFont);
  int x = width;
  const int wc = 46;
  // Segoe MDL2 Assets 캡션 글리프: 최소화 E921, 최대화 E922(복원 E923), 닫기 E8BB
  g_btns.push_back({ IDM_WCLOSE, L"\xE8BB", { x - wc, 0, x, kTopbarH }, 2 }); x -= wc;
  g_btns.push_back({ IDM_MAX,    L"\xE922", { x - wc, 0, x, kTopbarH }, 1 }); x -= wc;
  g_btns.push_back({ IDM_MIN,    L"\xE921", { x - wc, 0, x, kTopbarH }, 1 }); x -= wc;
  x -= 10;
  const int bh = 26, bt = (kTopbarH - bh) / 2;
  // 보기 모드 세그먼트 (미리/분할/편집)
  TopBtn views[] = {
    { IDM_VIEW_P, W(u8"미리"), {}, 3 },
    { IDM_VIEW_S, W(u8"분할"), {}, 3 },
    { IDM_VIEW_E, W(u8"편집"), {}, 3 },
  };
  for (TopBtn &v : views) {
    int w = textW(dc, v.label) + 16;
    v.rc = { x - w, bt, x, bt + bh };
    g_btns.push_back(v);
    x -= w + 2;
  }
  x -= 10;
  // 파일 버튼 (배열 앞 항목이 화면 오른쪽에 배치됨)
  TopBtn items[] = {
    { IDM_VSCODE, W(u8"VSCode"), {}, 0 },
    { IDM_SAVE,   W(u8"저장"),   {}, 0 },
    { IDM_OPEN,   W(u8"열기"),   {}, 0 },
    { IDM_NEW,    W(u8"새 파일"), {}, 0 },
    { IDM_SETTINGS,    W(u8"설정"), {}, 0 },
    { IDM_FORMATTABLE, W(u8"정렬"), {}, 0 },
    { IDM_INSERTTABLE, W(u8"표"),   {}, 0 },
  };
  for (TopBtn &it : items) {
    int w = textW(dc, it.label) + 18;
    it.rc = { x - w, bt, x, bt + bh };
    g_btns.push_back(it);
    x -= w + 4;
  }
  SelectObject(dc, old);
  ReleaseDC(g_hwnd, dc);
}
static int btnAt(int x, int y) {
  POINT p = { x, y };
  for (size_t i = 0; i < g_btns.size(); i++)
    if (PtInRect(&g_btns[i].rc, p)) return (int)i;
  return -1;
}
static int leftmostBtnX(int width) {
  int m = width;
  for (TopBtn &b : g_btns) m = std::min(m, (int)b.rc.left);
  return m;
}
static void paintTopbar(HDC dc, int width) {
  RECT bar = { 0, 0, width, kTopbarH };
  HBRUSH bg = CreateSolidBrush(themeTopbarBg());
  FillRect(dc, &bar, bg); DeleteObject(bg);
  HPEN pen = CreatePen(PS_SOLID, 1, themeBorder());
  HPEN oldPen = (HPEN)SelectObject(dc, pen);
  MoveToEx(dc, 0, kTopbarH - 1, nullptr); LineTo(dc, width, kTopbarH - 1);
  SelectObject(dc, oldPen); DeleteObject(pen);

  HFONT oldFont = (HFONT)SelectObject(dc, g_uiFont);
  SetBkMode(dc, TRANSPARENT);

  int fx = 12;
  if (g_dirty) {
    SetTextColor(dc, themeAccent());
    RECT dr = { fx, 0, fx + 14, kTopbarH };
    DrawTextW(dc, L"\x25CF", 1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fx += 16;
  }
  SetTextColor(dc, themeFg());
  std::wstring name = g_curName.empty() ? W(u8"제목 없음") : g_curName;
  RECT nr = { fx, 0, leftmostBtnX(width) - 8, kTopbarH };
  DrawTextW(dc, name.c_str(), -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  for (size_t i = 0; i < g_btns.size(); i++) {
    TopBtn &b = g_btns[i];
    bool hot = ((int)i == g_hotBtn);
    bool active = false;
    if (b.type == 3) {
      int bv = (b.id == IDM_VIEW_E) ? 0 : (b.id == IDM_VIEW_S) ? 1 : 2;
      active = (bv == g_view);
    }
    if (active) {
      HBRUSH hb = CreateSolidBrush(themeAccent());
      FillRect(dc, &b.rc, hb); DeleteObject(hb);
    } else if (hot) {
      COLORREF hc = (b.type == 2) ? RGB(0xe8, 0x11, 0x23) : themeBtnHover();
      HBRUSH hb = CreateSolidBrush(hc);
      FillRect(dc, &b.rc, hb); DeleteObject(hb);
    }
    COLORREF tc;
    if (active) tc = RGB(0xff, 0xff, 0xff);
    else if (b.type == 0) tc = themeFg();
    else tc = hot ? (b.type == 2 ? RGB(0xff, 0xff, 0xff) : themeFg()) : themeMuted();
    SetTextColor(dc, tc);
    const wchar_t *glyph = b.label.c_str();
    if (b.id == IDM_MAX && IsZoomed(g_hwnd)) glyph = L"\xE923"; // 최대화 상태면 복원 아이콘
    SelectObject(dc, (b.type == 1 || b.type == 2) ? g_glyphFont : g_uiFont);
    DrawTextW(dc, glyph, -1, &b.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(dc, oldFont);
}

// ---------------------------------------------------------------------------
// 프리뷰 (WebView2) 브리지 + 보기 레이아웃
// ---------------------------------------------------------------------------
// 미리보기 외부 링크를 기본 브라우저로 (http/https/mailto 만 허용)
static std::string onOpenExternalBind(const std::string &req) {
  std::string url = base64_decode(firstStringArg(req));
  auto startsWith = [&](const char *p) {
    size_t n = std::strlen(p);
    return url.size() >= n && _strnicmp(url.c_str(), p, (int)n) == 0;
  };
  if (!startsWith("http://") && !startsWith("https://") && !startsWith("mailto:"))
    return "{\"ok\":false}";
  ShellExecuteW(g_hwnd, L"open", utf8_to_wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return "{\"ok\":true}";
}

static void pushPreviewNow() {
  if (!g_webview || !g_webviewReady) return;
  g_webview->eval("window.mymdRender&&window.mymdRender(\"" + base64_encode(getEditText()) + "\")");
}
static void pushPreviewConfig() {
  if (!g_webview || !g_webviewReady) return;
  g_webview->eval(std::string("window.mymdSetTheme&&window.mymdSetTheme(\"") +
                  (isDarkTheme() ? "dark" : "light") + "\")");
  g_webview->eval("window.mymdSetLangs&&window.mymdSetLangs(\"" + base64_encode(g_langsJson) + "\")");
  std::string base = g_curDir.empty() ? std::string() : (toFileUrl(g_curDir) + "/");
  g_webview->eval("window.mymdSetBase&&window.mymdSetBase(\"" + base64_encode(base) + "\")");
}
static void refreshPreview() {
  if (g_view == 0) return;
  pushPreviewConfig();
  pushPreviewNow();
}
static std::string onPreviewReady(const std::string &) {
  g_webviewReady = true;
  pushPreviewConfig();
  pushPreviewNow();
  return "true";
}
static void ensureWebview() {
  if (g_webview) return;
  g_webview = new webview::webview(false, (void *)&g_preview); // 외부 창(우측 패널)에 임베드
  g_webview->bind("mymdPreviewReady", [](std::string r) { return onPreviewReady(r); });
  g_webview->bind("mymdOpenExternal", [](std::string r) { return onOpenExternalBind(r); });
  g_webview->navigate(toFileUrl(exeDir() + L"\\web\\preview.html"));
  SetFocus(g_edit); // 생성 시 프리뷰로 간 포커스 복귀
}

// 보기 모드에 따라 에디터/디바이더/프리뷰 배치
static void layout() {
  if (!g_edit) return;
  RECT cr; GetClientRect(g_hwnd, &cr);
  int W = cr.right, H = cr.bottom;
  int top = kTopbarH, ch = H - kTopbarH; if (ch < 0) ch = 0;
  layoutTopbar(W);
  g_dividerX = -1;
  if (g_view == 0) {              // 에디터만
    MoveWindow(g_edit, 0, top, W, ch, TRUE); ShowWindow(g_edit, SW_SHOW);
    if (g_preview) ShowWindow(g_preview, SW_HIDE);
  } else if (g_view == 2) {       // 미리보기만
    ShowWindow(g_edit, SW_HIDE);
    if (g_preview) { MoveWindow(g_preview, 0, top, W, ch, TRUE); ShowWindow(g_preview, SW_SHOW); }
  } else {                        // 분할
    int ew = (int)(W * g_splitRatio), minw = 120;
    if (ew < minw) ew = minw;
    if (ew > W - minw - kDividerW) ew = W - minw - kDividerW;
    if (ew < 0) ew = 0;
    MoveWindow(g_edit, 0, top, ew, ch, TRUE); ShowWindow(g_edit, SW_SHOW);
    int px = ew + kDividerW;
    if (g_preview) { MoveWindow(g_preview, px, top, W - px, ch, TRUE); ShowWindow(g_preview, SW_SHOW); }
    g_dividerX = ew;
  }
  if (g_webview) g_webview->update_bounds();
  InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void setView(int v) {
  g_view = v;
  if (v != 0) ensureWebview(); // 에디터 전용이 아니면 프리뷰 엔진 지연 생성
  layout();
  refreshPreview();
  SetFocus(g_edit);
}

// ---------------------------------------------------------------------------
// 에디터 동작 (EDIT 서브클래스): Tab 들여쓰기, 리스트 자동 이어쓰기
// ---------------------------------------------------------------------------
static WNDPROC g_editProc = nullptr;
static bool g_swallowChar = false;

static std::wstring editGetTextW() {
  int len = GetWindowTextLengthW(g_edit);
  std::wstring w; w.resize(len + 1);
  int got = GetWindowTextW(g_edit, &w[0], len + 1);
  w.resize(got);
  return w;
}
static void editGetSel(DWORD &a, DWORD &b) {
  SendMessageW(g_edit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
}
static std::wstring rtrimWs(const std::wstring &s) {
  size_t e = s.size();
  while (e > 0 && (s[e-1]==L' '||s[e-1]==L'\t'||s[e-1]==L'\r'||s[e-1]==L'\n')) e--;
  return s.substr(0, e);
}

// 리스트 항목 파싱
struct ListItem {
  std::wstring indent;
  bool ordered = false;
  wchar_t delim = L'.';
  int num = 0;
  std::wstring rest; // ordered 재구성용 (마커 뒤 내용)
};
static bool parseListItem(const std::wstring &line, ListItem &it) {
  size_t i = 0;
  while (i < line.size() && (line[i]==L' '||line[i]==L'\t')) i++;
  it.indent = line.substr(0, i);
  if (i < line.size() && (line[i]==L'-'||line[i]==L'*'||line[i]==L'+')) {
    size_t k = i + 1;
    if (k < line.size() && line[k] == L' ') { it.ordered = false; return true; }
  }
  size_t j = i; while (j < line.size() && line[j] >= L'0' && line[j] <= L'9') j++;
  if (j > i && j < line.size() && (line[j]==L'.'||line[j]==L')')) {
    size_t k = j + 1;
    if (k < line.size() && line[k] == L' ') {
      it.ordered = true; it.delim = line[j];
      it.num = 0; for (size_t t = i; t < j; t++) it.num = it.num * 10 + (line[t] - L'0');
      while (k < line.size() && line[k]==L' ') k++;
      it.rest = line.substr(k);
      return true;
    }
  }
  return false;
}
static int leadingWsLen(const std::wstring &s) {
  size_t i = 0; while (i < s.size() && (s[i]==L' '||s[i]==L'\t')) i++; return (int)i;
}
// 새 들여쓰기 레벨에서 ordered 시작 번호 (이전 형제가 ordered면 +1, 아니면 1)
static int orderedStartNum(const std::vector<std::wstring> &lines, int curIdx, int newIndentLen) {
  for (int i = curIdx - 1; i >= 0; i--) {
    if (rtrimWs(lines[i]).empty()) continue;
    int len = leadingWsLen(lines[i]);
    if (len < newIndentLen) return 1;        // 부모 레벨 도달
    if (len == newIndentLen) {
      ListItem it;
      if (parseListItem(lines[i], it) && it.ordered) return it.num + 1;
      return 1;
    }
    // len > newIndentLen: 더 깊음, 건너뜀
  }
  return 1;
}
static std::vector<std::wstring> splitLines(const std::wstring &text) {
  std::vector<std::wstring> lines; size_t st = 0;
  for (size_t i = 0; i <= text.size(); i++) {
    if (i == text.size() || text[i] == L'\n') {
      std::wstring ln = text.substr(st, i - st);
      if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
      lines.push_back(ln); st = i + 1;
    }
  }
  return lines;
}
// 선택 범위를 줄 단위로 들여쓰기/내어쓰기
static void blockIndent(const std::wstring &text, DWORD a, DWORD b, bool shift) {
  DWORD ls = a; while (ls > 0 && text[ls-1] != L'\n') ls--;
  DWORD le = b;
  if (le > a && le > 0 && text[le-1] == L'\n') le--;
  while (le < text.size() && text[le] != L'\n') le++;
  std::vector<std::wstring> lines = splitLines(text.substr(ls, le - ls));
  std::wstring indent((size_t)g_tabSize, L' ');
  for (std::wstring &ln : lines) {
    if (!shift) ln = indent + ln;
    else if (!ln.empty() && ln[0] == L'\t') ln.erase(0, 1);
    else { int n = 0; while (n < g_tabSize && n < (int)ln.size() && ln[n] == L' ') n++; ln.erase(0, n); }
  }
  std::wstring out;
  for (size_t i = 0; i < lines.size(); i++) { if (i) out += L"\r\n"; out += lines[i]; }
  SendMessageW(g_edit, EM_SETSEL, ls, le);
  SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)out.c_str());
  SendMessageW(g_edit, EM_SETSEL, ls, ls + (DWORD)out.size());
}

// Tab: 현재 줄이 리스트 항목이면 항목 전체를 들여쓰기/내어쓰기(ordered 번호 보정).
// 리스트가 아니면 캐럿에 공백 N칸. 멀티라인 선택은 블록 들여쓰기.
static void doTabIndent(bool shift) {
  std::wstring text = editGetTextW();
  DWORD a, b; editGetSel(a, b);
  if (a != b) {
    bool multiline = false;
    for (DWORD i = a; i < b && i < text.size(); i++) if (text[i] == L'\n') { multiline = true; break; }
    if (multiline) { blockIndent(text, a, b, shift); return; }
  }

  DWORD ls = a; while (ls > 0 && text[ls-1] != L'\n') ls--;
  DWORD le = a; while (le < text.size() && text[le] != L'\n') le++;
  DWORD lineEnd = le; if (lineEnd > ls && text[lineEnd-1] == L'\r') lineEnd--;
  std::wstring line = text.substr(ls, lineEnd - ls);

  ListItem it;
  if (a == b && parseListItem(line, it)) {
    int oldLen = (int)it.indent.size();
    if (shift && oldLen == 0) return; // 더 내어쓸 수 없음
    int newLen = shift ? (oldLen - g_tabSize) : (oldLen + g_tabSize);
    if (newLen < 0) newLen = 0;

    std::vector<std::wstring> lines = splitLines(text);
    int curIdx = 0;
    for (DWORD i = 0; i < a && i < text.size(); i++) if (text[i] == L'\n') curIdx++;

    std::wstring newIndent((size_t)newLen, L' ');
    std::wstring newLine;
    if (it.ordered) {
      int num = orderedStartNum(lines, curIdx, newLen);
      newLine = newIndent + std::to_wstring(num) + std::wstring(1, it.delim) + L" " + it.rest;
    } else {
      newLine = newIndent + line.substr(oldLen); // 마커/내용/체크박스 그대로, 들여쓰기만 변경
    }
    LRESULT caret = (LRESULT)a + ((LRESULT)newLine.size() - (LRESULT)line.size());
    if (caret < (LRESULT)ls) caret = ls;
    SendMessageW(g_edit, EM_SETSEL, ls, lineEnd);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)newLine.c_str());
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
    return;
  }

  if (!shift) {
    std::wstring sp((size_t)g_tabSize, L' ');
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)sp.c_str());
    return;
  }
  blockIndent(text, a, b, true); // Shift+Tab 비리스트: 현재 줄 내어쓰기
}

// Enter: 목록 항목이면 같은 마커로 이어쓰고, 빈 항목이면 마커를 제거(리스트 종료).
static bool doListEnter() {
  DWORD a, b; editGetSel(a, b);
  if (a != b) return false;
  std::wstring text = editGetTextW();
  DWORD ls = a; while (ls > 0 && text[ls-1] != L'\n') ls--;
  DWORD le = a; while (le < text.size() && text[le] != L'\n') le++;
  std::wstring line = text.substr(ls, le - ls);
  if (!line.empty() && line.back() == L'\r') line.pop_back();

  size_t i = 0;
  while (i < line.size() && (line[i]==L' '||line[i]==L'\t')) i++;
  std::wstring indent = line.substr(0, i);
  auto endList = [&]() { SendMessageW(g_edit, EM_SETSEL, ls, le); SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L""); };
  auto cont = [&](const std::wstring &marker) { std::wstring ins = L"\r\n" + marker; SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str()); };

  if (i < line.size() && (line[i]==L'-'||line[i]==L'*'||line[i]==L'+')) {
    wchar_t marker = line[i];
    size_t k = i + 1;
    if (k < line.size() && line[k] == L' ') {
      while (k < line.size() && line[k] == L' ') k++;
      if (k + 2 < line.size() && line[k]==L'[' &&
          (line[k+1]==L' '||line[k+1]==L'x'||line[k+1]==L'X') && line[k+2]==L']') {
        size_t r = k + 3; if (r < line.size() && line[r]==L' ') r++;
        if (rtrimWs(line.substr(r)).empty()) { endList(); return true; }
        cont(indent + std::wstring(1, marker) + L" [ ] ");
        return true;
      }
      if (rtrimWs(line.substr(k)).empty()) { endList(); return true; }
      cont(indent + std::wstring(1, marker) + L" ");
      return true;
    }
  }
  size_t j = i; while (j < line.size() && line[j] >= L'0' && line[j] <= L'9') j++;
  if (j > i && j < line.size() && (line[j]==L'.'||line[j]==L')')) {
    wchar_t delim = line[j];
    size_t k = j + 1;
    if (k < line.size() && line[k] == L' ') {
      while (k < line.size() && line[k]==L' ') k++;
      if (rtrimWs(line.substr(k)).empty()) { endList(); return true; }
      int num = 0; for (size_t t = i; t < j; t++) num = num * 10 + (line[t] - L'0');
      cont(indent + std::to_wstring(num + 1) + std::wstring(1, delim) + L" ");
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// 표 편의 기능 (정렬, Tab/Enter 셀 이동/행 추가, 삽입) - web/legacy/app.js 이식
//   EDIT 버퍼는 \r\n 줄바꿈을 쓴다. TblLine.text 는 \r 를 뺀 줄 내용,
//   TblLine.start 는 버퍼 절대 오프셋(EM_SETSEL 좌표와 동일).
// ---------------------------------------------------------------------------
struct TblLine { std::wstring text; DWORD start; };

static std::wstring tblTrim(const std::wstring &s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a]==L' '||s[a]==L'\t')) a++;
  while (b > a && (s[b-1]==L' '||s[b-1]==L'\t')) b--;
  return s.substr(a, b - a);
}
static std::vector<TblLine> tblGetLines(const std::wstring &val) {
  std::vector<TblLine> out;
  DWORD start = 0;
  for (DWORD i = 0; i <= (DWORD)val.size(); i++) {
    if (i == (DWORD)val.size() || val[i] == L'\n') {
      DWORD end = i;
      if (end > start && val[end-1] == L'\r') end--; // 줄 내용은 \r 제외
      out.push_back({ val.substr(start, end - start), start });
      start = i + 1;
    }
  }
  return out;
}
static int tblLineIndexAt(const std::vector<TblLine> &lines, DWORD pos) {
  for (int k = 0; k < (int)lines.size(); k++) {
    const TblLine &L = lines[k];
    if (pos >= L.start && pos <= L.start + (DWORD)L.text.size()) return k;
  }
  return (int)lines.size() - 1;
}
static bool tblIsRow(const std::wstring &text) {
  std::wstring t = tblTrim(text);
  if (t.find(L'|') == std::wstring::npos) return false;
  if (!t.empty() && t[0]==L'|') return true;
  int cnt = 0; for (wchar_t c : t) if (c==L'|') cnt++;
  return cnt >= 2;
}
static std::vector<std::wstring> tblParseCells(const std::wstring &line) {
  std::wstring t = tblTrim(line);
  if (!t.empty() && t.front()==L'|') t = t.substr(1);
  if (!t.empty() && t.back()==L'|') t = t.substr(0, t.size()-1);
  std::vector<std::wstring> cells;
  size_t s = 0;
  for (size_t i = 0; i <= t.size(); i++) {
    if (i == t.size() || t[i]==L'|') { cells.push_back(tblTrim(t.substr(s, i-s))); s = i + 1; }
  }
  return cells;
}
static bool tblIsSep(const std::wstring &text) {
  std::vector<std::wstring> cells = tblParseCells(text);
  if (cells.empty()) return false;
  for (auto &c0 : cells) {                       // /^:?-{1,}:?$/
    std::wstring c = tblTrim(c0);
    size_t i = 0;
    if (i < c.size() && c[i]==L':') i++;
    size_t dash = 0; while (i < c.size() && c[i]==L'-') { i++; dash++; }
    if (dash < 1) return false;
    if (i < c.size() && c[i]==L':') i++;
    if (i != c.size()) return false;
  }
  return true;
}
static std::vector<int> tblPipes(const std::wstring &text) {
  std::vector<int> p;
  for (int i = 0; i < (int)text.size(); i++)
    if (text[i]==L'|' && (i==0 || text[i-1]!=L'\\')) p.push_back(i);
  return p;
}
static void tblBounds(const std::vector<TblLine> &lines, int idx, int &top, int &bot) {
  top = idx; bot = idx;
  while (top > 0 && tblIsRow(lines[top-1].text)) top--;
  while (bot < (int)lines.size()-1 && tblIsRow(lines[bot+1].text)) bot++;
}
static int tblChW(unsigned int c) {                // 전각(한글/CJK) 폭 2
  if ((c>=0x1100&&c<=0x115F)||(c>=0x2E80&&c<=0xA4CF)||(c>=0xAC00&&c<=0xD7A3)||
      (c>=0xF900&&c<=0xFAFF)||(c>=0xFE30&&c<=0xFE4F)||(c>=0xFF00&&c<=0xFF60)||
      (c>=0xFFE0&&c<=0xFFE6)||(c>=0x20000&&c<=0x3FFFD)) return 2;
  return 1;
}
static int tblW(const std::wstring &s) {
  int w = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned int c = (unsigned int)s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i+1 < s.size()) {  // 서로게이트 쌍 결합
      unsigned int lo = (unsigned int)s[i+1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c-0xD800)<<10) + (lo-0xDC00); i++; }
    }
    w += tblChW(c);
  }
  return w;
}
static std::wstring tblPad(const std::wstring &s, int n) {
  int t = n - tblW(s); if (t < 0) t = 0; return s + std::wstring((size_t)t, L' ');
}
static std::wstring tblPadC(const std::wstring &s, int n) {
  int t = n - tblW(s); if (t < 0) t = 0; int l = t/2;
  return std::wstring((size_t)l, L' ') + s + std::wstring((size_t)(t-l), L' ');
}
static void tblReplace(DWORD a, DWORD b, const std::wstring &text) {
  SendMessageW(g_edit, EM_SETSEL, a, b);
  SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)text.c_str()); // Undo 보존
}
static bool tblSelectCell(const TblLine &line, int cellIdx) {
  std::vector<int> pipes = tblPipes(line.text);
  if (cellIdx < 0 || cellIdx > (int)pipes.size()-2) return false;
  int cs = pipes[cellIdx]+1, ce = pipes[cellIdx+1];
  int s = cs; while (s < ce && line.text[s]==L' ') s++;
  int e = ce; while (e > s && line.text[e-1]==L' ') e--;
  SendMessageW(g_edit, EM_SETSEL, (WPARAM)(line.start+s), (LPARAM)(line.start+e));
  return true;
}
static int tblPrevRow(const std::vector<TblLine> &lines, int from, int top) {
  for (int k = from; k >= top; k--) if (!tblIsSep(lines[k].text)) return k;
  return -1;
}

// 커서가 속한 표를 셀 폭에 맞춰 재정렬(정렬 방향 유지, 구분선 없으면 생성).
static bool doFormatTable() {
  std::wstring val = editGetTextW();
  DWORD a, b; editGetSel(a, b);
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot; tblBounds(lines, ci, top, bot);
  struct Row { std::vector<std::wstring> cells; bool sep; };
  std::vector<Row> rows; int sepAt = -1;
  for (int k = top; k <= bot; k++) {
    bool sep = tblIsSep(lines[k].text);
    if (sep && sepAt < 0) sepAt = (int)rows.size();
    rows.push_back({ tblParseCells(lines[k].text), sep });
  }
  int cols = 0; for (auto &r : rows) cols = std::max(cols, (int)r.cells.size());
  std::vector<int> align(cols, 0);                 // 0 none, 1 left, 2 right, 3 center
  if (sepAt >= 0) {
    auto &sc = rows[sepAt].cells;
    for (int c = 0; c < cols; c++) {
      std::wstring x = c < (int)sc.size() ? tblTrim(sc[c]) : L"";
      bool L_ = !x.empty() && x.front()==L':', R_ = !x.empty() && x.back()==L':';
      align[c] = (L_&&R_) ? 3 : R_ ? 2 : L_ ? 1 : 0;
    }
  }
  std::vector<int> width(cols, 3);
  for (auto &r : rows) {
    if (r.sep) continue;
    for (int c = 0; c < cols; c++) {
      std::wstring cell = c < (int)r.cells.size() ? r.cells[c] : L"";
      width[c] = std::max(width[c], tblW(cell));
    }
  }
  std::vector<std::wstring> out;
  for (auto &r : rows) {
    std::vector<std::wstring> segs;
    if (r.sep) {
      for (int c = 0; c < cols; c++) {
        int w = width[c]; std::wstring d;
        if (align[c]==3)      d = L":" + std::wstring((size_t)std::max(1, w-2), L'-') + L":";
        else if (align[c]==2) d = std::wstring((size_t)std::max(2, w-1), L'-') + L":";
        else if (align[c]==1) d = L":" + std::wstring((size_t)std::max(2, w-1), L'-');
        else                  d = std::wstring((size_t)std::max(3, w), L'-');
        segs.push_back(d);
      }
    } else {
      for (int c = 0; c < cols; c++) {
        std::wstring cell = c < (int)r.cells.size() ? r.cells[c] : L"";
        if (align[c]==2)      { int pad = width[c]-tblW(cell); if (pad<0) pad=0; segs.push_back(std::wstring((size_t)pad, L' ') + cell); }
        else if (align[c]==3) segs.push_back(tblPadC(cell, width[c]));
        else                  segs.push_back(tblPad(cell, width[c]));
      }
    }
    std::wstring ln = L"| ";
    for (size_t i = 0; i < segs.size(); i++) { if (i) ln += L" | "; ln += segs[i]; }
    ln += L" |";
    out.push_back(ln);
  }
  if (sepAt < 0) {
    std::vector<std::wstring> segs;
    for (int c = 0; c < cols; c++) segs.push_back(std::wstring((size_t)std::max(3, width[c]), L'-'));
    std::wstring ln = L"| ";
    for (size_t i = 0; i < segs.size(); i++) { if (i) ln += L" | "; ln += segs[i]; }
    ln += L" |";
    out.insert(out.begin()+1, ln);
  }
  DWORD blockStart = lines[top].start;
  DWORD blockEnd = lines[bot].start + (DWORD)lines[bot].text.size();
  std::wstring joined;
  for (size_t i = 0; i < out.size(); i++) { if (i) joined += L"\r\n"; joined += out[i]; }
  tblReplace(blockStart, blockEnd, joined);
  SendMessageW(g_edit, EM_SETSEL, blockStart, blockStart);
  return true;
}

// 표 골격 삽입(헤더 + 구분선 + 빈 본문). 첫 헤더 셀을 선택.
static bool insertTableSkeleton(int cols, int rows) {
  std::wstring header = L"| ";
  for (int i = 0; i < cols; i++) { if (i) header += L" | "; header += L"제목" + std::to_wstring(i+1); }
  header += L" |";
  std::wstring sep = L"| ";
  for (int i = 0; i < cols; i++) { if (i) sep += L" | "; sep += L"---"; }
  sep += L" |";
  std::vector<std::wstring> body;
  for (int r = 0; r < rows; r++) {
    std::wstring row = L"| ";
    for (int i = 0; i < cols; i++) { if (i) row += L" | "; row += L"  "; }
    row += L" |";
    body.push_back(row);
  }
  DWORD pos, selEnd; editGetSel(pos, selEnd);
  std::wstring val = editGetTextW();
  bool atStart = (pos == 0) || (pos <= (DWORD)val.size() && val[pos-1]==L'\n');
  std::wstring prefix = atStart ? L"" : L"\r\n";
  std::wstring text = prefix + header + L"\r\n" + sep;
  for (auto &bln : body) text += L"\r\n" + bln;
  text += L"\r\n";
  tblReplace(pos, pos, text);
  DWORD firstCell = pos + (DWORD)prefix.size() + 2;  // "| " 다음
  std::wstring first = L"제목1";
  SendMessageW(g_edit, EM_SETSEL, (WPARAM)firstCell, (LPARAM)(firstCell + (DWORD)first.size()));
  SetFocus(g_edit);
  return true;
}

// 마지막 열에서 Tab: 모든 행에 빈 셀 추가 후(구분선 있으면 재정렬) 같은 행 새 셀 선택.
static bool tableAddColumn(const std::vector<TblLine> &lines, int top, int bot, int ci) {
  bool hasSep = false;
  std::vector<std::wstring> out;
  for (int k = top; k <= bot; k++) {
    bool sep = tblIsSep(lines[k].text);
    if (sep) hasSep = true;
    std::wstring t = tblTrim(lines[k].text);
    if (t.empty() || t.front()!=L'|') t = L"| " + t;
    if (t.empty() || t.back()!=L'|') t = t + L" |";
    out.push_back(sep ? (t + L" --- |") : (t + L"   |"));
  }
  DWORD blockStart = lines[top].start;
  DWORD blockEnd = lines[bot].start + (DWORD)lines[bot].text.size();
  std::wstring joined;
  for (size_t i = 0; i < out.size(); i++) { if (i) joined += L"\r\n"; joined += out[i]; }
  tblReplace(blockStart, blockEnd, joined);
  if (hasSep) { SendMessageW(g_edit, EM_SETSEL, blockStart, blockStart); doFormatTable(); }
  std::vector<TblLine> lines2 = tblGetLines(editGetTextW());
  if (ci >= 0 && ci < (int)lines2.size()) {
    const TblLine &tgt = lines2[ci];
    std::vector<int> pp = tblPipes(tgt.text);
    tblSelectCell(tgt, (int)pp.size()-2);
  }
  return true;
}

// Tab/Shift+Tab 셀 이동. 표가 아니면 false.
static bool doTableNav(bool shift) {
  std::wstring val = editGetTextW();
  DWORD a, b; editGetSel(a, b);
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot; tblBounds(lines, ci, top, bot);
  const TblLine &line = lines[ci];
  std::vector<int> pipes = tblPipes(line.text);
  int col = (int)a - (int)line.start;
  if ((int)pipes.size() < 2) return false;
  int cell = -1;
  for (int k = 0; k < (int)pipes.size()-1; k++) { if (col >= pipes[k] && col <= pipes[k+1]) { cell = k; break; } }
  if (cell < 0) { tblSelectCell(line, 0); return true; }
  if (!shift) {
    if (cell+1 <= (int)pipes.size()-2) { tblSelectCell(line, cell+1); return true; }
    return tableAddColumn(lines, top, bot, ci);  // 마지막 열: 열 추가
  }
  if (cell-1 >= 0) { tblSelectCell(line, cell-1); return true; }
  int pr = tblPrevRow(lines, ci-1, top);
  if (pr >= 0) { const TblLine &pl = lines[pr]; std::vector<int> pp = tblPipes(pl.text); tblSelectCell(pl, (int)pp.size()-2); return true; }
  return true;
}

// 표 행에서 Enter: 같은 열 수 빈 행 추가(구분선 없으면 구분선 + 빈 행). 표가 아니면 false.
static bool doTableEnter() {
  std::wstring val = editGetTextW();
  DWORD a, b; editGetSel(a, b);
  if (a != b) return false;
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot; tblBounds(lines, ci, top, bot);
  int sepIdx = -1; for (int k = top; k <= bot; k++) { if (tblIsSep(lines[k].text)) { sepIdx = k; break; } }
  int cols = (int)tblParseCells(lines[top].text).size();
  std::wstring emptyRow = L"|"; for (int i = 0; i < cols; i++) emptyRow += L"  |";
  DWORD insertPos, rowStart; std::wstring insertText;
  if (sepIdx < 0) {
    std::wstring sep = L"| "; for (int i = 0; i < cols; i++) { if (i) sep += L" | "; sep += L"---"; } sep += L" |";
    insertPos = lines[ci].start + (DWORD)lines[ci].text.size();
    insertText = L"\r\n" + sep + L"\r\n" + emptyRow;
    rowStart = insertPos + 2 + (DWORD)sep.size() + 2;
  } else {
    int anchor = (ci < sepIdx) ? sepIdx : ci;
    insertPos = lines[anchor].start + (DWORD)lines[anchor].text.size();
    insertText = L"\r\n" + emptyRow;
    rowStart = insertPos + 2;
  }
  tblReplace(insertPos, insertPos, insertText);
  DWORD caret = rowStart + 1;                       // 새 행 첫 셀(| 다음)
  SendMessageW(g_edit, EM_SETSEL, caret, caret);
  return true;
}

// ---------------------------------------------------------------------------
// 스크롤 동기화 (에디터 -> 프리뷰). 분할 보기 + settings.scrollSync 일 때만.
// ---------------------------------------------------------------------------
static void requestScrollSync() {
  if (!g_scrollSync || g_view != 1) return;
  SetTimer(g_hwnd, IDT_SCROLLSYNC, 16, nullptr); // 연속 이벤트 합치기(디바운스)
}
static void syncPreviewScroll() {
  if (!g_scrollSync || g_view != 1 || !g_webview || !g_webviewReady) return;
  SCROLLINFO si = { sizeof(si) };
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (!GetScrollInfo(g_edit, SB_VERT, &si)) return;
  int denom = si.nMax - si.nMin - (int)si.nPage + 1; // 최대 스크롤 위치(라인 단위)
  double ratio = denom > 0 ? (double)(si.nPos - si.nMin) / (double)denom : 0.0;
  if (ratio < 0.0) ratio = 0.0;
  if (ratio > 1.0) ratio = 1.0;
  char buf[64]; snprintf(buf, sizeof(buf), "%.5f", ratio);
  g_webview->eval(std::string("window.mymdScrollTo&&window.mymdScrollTo(") + buf + ")");
}

static LRESULT CALLBACK EditProc(HWND e, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_KEYDOWN:
      if (w == VK_TAB) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (doTableNav(shift)) { g_swallowChar = true; return 0; }  // 표 우선
        doTabIndent(shift);
        g_swallowChar = true;
        return 0;
      }
      if (w == VK_RETURN) {
        if (doTableEnter()) { g_swallowChar = true; return 0; }     // 표 우선
        if (doListEnter()) { g_swallowChar = true; return 0; }
      }
      if (w == VK_UP || w == VK_DOWN || w == VK_PRIOR || w == VK_NEXT ||
          w == VK_HOME || w == VK_END) {                            // 키보드 스크롤
        LRESULT r = CallWindowProcW(g_editProc, e, m, w, l);
        requestScrollSync();
        return r;
      }
      break;
    case WM_CHAR:
      if (g_swallowChar) { g_swallowChar = false; return 0; }
      break;
    case WM_VSCROLL:
    case WM_MOUSEWHEEL: {                                           // 스크롤바/휠
      LRESULT r = CallWindowProcW(g_editProc, e, m, w, l);
      requestScrollSync();
      return r;
    }
  }
  return CallWindowProcW(g_editProc, e, m, w, l);
}

// ---------------------------------------------------------------------------
// 메인 창 프로시저
// ---------------------------------------------------------------------------
static void applyEditStyle() {
  if (g_editFont) DeleteObject(g_editFont);
  g_editFont = makeEditFont(g_fontSize);
  SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
  DWORD tw = (DWORD)(g_tabSize * 4); // 대략 N칸(다이얼로그 단위)
  SendMessageW(g_edit, EM_SETTABSTOPS, 1, (LPARAM)&tw);
  SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(10, 10));
}

// EDIT 컨트롤 생성(서브클래스 포함). wrap 토글은 스타일이 생성 시 고정이라 재생성으로 처리.
static void createEdit(HWND parent) {
  DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                ES_NOHIDESEL | ES_WANTRETURN | ES_AUTOVSCROLL;
  if (!g_wrap) style |= ES_AUTOHSCROLL | WS_HSCROLL;
  g_edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)1, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(g_edit, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFE, 0);
  applyEditStyle();
  g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
}
// wrap 변경 시 본문/선택/더티를 보존하며 EDIT 를 재생성.
static void recreateEditForWrap() {
  if (!g_edit) return;
  std::wstring text = editGetTextW();
  DWORD a, b; editGetSel(a, b);
  bool wasDirty = g_dirty;
  DestroyWindow(g_edit);
  createEdit(g_hwnd);
  g_suppressDirty = true;
  SetWindowTextW(g_edit, text.c_str());
  g_suppressDirty = false;
  g_dirty = wasDirty;
  SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
  SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
  layout();
  SetFocus(g_edit);
}

// ---------------------------------------------------------------------------
// 표 삽입 대화상자 (프로그램 생성 모달)
//   .rc 의 windres 한글 인코딩을 피하려고 W(u8"...") 와이드 문자열로 직접 만든다.
//   IsDialogMessage 로 Tab 이동을 받고, Enter=삽입 / Esc=취소는 루프에서 처리.
// ---------------------------------------------------------------------------
static int dlgClamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int dlgReadInt(HWND e, int dft) {
  wchar_t buf[16]; int n = GetWindowTextW(e, buf, 15);
  int v = 0; bool any = false;
  for (int i = 0; i < n; i++) if (buf[i] >= L'0' && buf[i] <= L'9') { v = v*10 + (buf[i]-L'0'); any = true; }
  return any ? v : dft;
}
static bool g_tblDlgDone, g_tblDlgOk;
static LRESULT CALLBACK TableDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_COMMAND) {
    if (LOWORD(w) == IDOK)     { g_tblDlgOk = true;  g_tblDlgDone = true; return 0; }
    if (LOWORD(w) == IDCANCEL) { g_tblDlgOk = false; g_tblDlgDone = true; return 0; }
  } else if (m == WM_CLOSE) {
    g_tblDlgOk = false; g_tblDlgDone = true; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
static bool showTableDialog(int &cols, int &rows) {
  static bool reg = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TableDlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MyMDTableDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  int dw = 230, dh = 150;
  RECT pr; GetWindowRect(g_hwnd, &pr);
  int px = pr.left + ((pr.right - pr.left) - dw) / 2;
  int py = pr.top + ((pr.bottom - pr.top) - dh) / 2;
  HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
    L"MyMDTableDlg", W(u8"표 삽입").c_str(),
    WS_POPUP | WS_CAPTION | WS_SYSMENU, px, py, dw, dh,
    g_hwnd, nullptr, hi, nullptr);
  if (!hDlg) return false;

  auto mk = [&](const wchar_t *cls, const std::wstring &txt, DWORD style, int x, int y, int w2, int h2, int id) {
    HWND c = CreateWindowExW(0, cls, txt.c_str(), WS_CHILD | WS_VISIBLE | style,
      x, y, w2, h2, hDlg, (HMENU)(INT_PTR)id, hi, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return c;
  };
  mk(L"STATIC", W(u8"열 수"), SS_RIGHT, 14, 20, 44, 18, -1);
  HWND eCols = mk(L"EDIT", L"2", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 66, 18, 56, 24, 1001);
  mk(L"STATIC", W(u8"행 수"), SS_RIGHT, 14, 52, 44, 18, -1);
  HWND eRows = mk(L"EDIT", L"2", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 66, 50, 56, 24, 1002);
  mk(L"BUTTON", W(u8"삽입"), BS_DEFPUSHBUTTON | WS_TABSTOP, 138, 18, 76, 26, IDOK);
  mk(L"BUTTON", W(u8"취소"), WS_TABSTOP, 138, 50, 76, 26, IDCANCEL);

  SetFocus(eCols);
  SendMessageW(eCols, EM_SETSEL, 0, -1);
  EnableWindow(g_hwnd, FALSE);
  ShowWindow(hDlg, SW_SHOW);

  g_tblDlgDone = false; g_tblDlgOk = false;
  MSG msg;
  while (!g_tblDlgDone) {
    BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r == 0) { PostQuitMessage((int)msg.wParam); break; }   // 앱 종료 전파
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { g_tblDlgOk = true; break; }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { g_tblDlgOk = false; break; }
    if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }

  if (g_tblDlgOk) {
    cols = dlgClamp(dlgReadInt(eCols, 2), 1, 20);
    rows = dlgClamp(dlgReadInt(eRows, 2), 1, 50);
  }
  EnableWindow(g_hwnd, TRUE);
  DestroyWindow(hDlg);
  SetForegroundWindow(g_hwnd);
  SetFocus(g_edit);
  return g_tblDlgOk;
}

// ---------------------------------------------------------------------------
// 설정 대화상자 (프로그램 생성 모달) + 런타임 적용
// ---------------------------------------------------------------------------
static std::vector<std::string> parseLangsJson(const std::string &arr) {
  std::vector<std::string> out;
  for (size_t i = 0; i < arr.size(); ) {
    if (arr[i] == '"') {
      std::string s; size_t j = i + 1;
      while (j < arr.size() && arr[j] != '"') {
        if (arr[j] == '\\' && j+1 < arr.size()) { s += arr[j+1]; j += 2; }
        else { s += arr[j]; j++; }
      }
      out.push_back(s); i = j + 1;
    } else i++;
  }
  return out;
}
static std::string csvToLangsJson(const std::string &csv) {
  std::string o = "["; bool first = true;
  std::string tok;
  auto flush = [&]() {
    size_t a = 0, b = tok.size();
    while (a < b && (tok[a]==' '||tok[a]=='\t')) a++;
    while (b > a && (tok[b-1]==' '||tok[b-1]=='\t')) b--;
    std::string t = tok.substr(a, b - a);
    if (!t.empty()) { if (!first) o += ","; o += "\"" + jsonEscape(t) + "\""; first = false; }
    tok.clear();
  };
  for (char c : csv) { if (c == ',') flush(); else tok += c; }
  flush();
  o += "]";
  return o;
}

static int comboIndex(const std::string &v, const char *const *opts, int n, int dft) {
  for (int i = 0; i < n; i++) if (v == opts[i]) return i;
  return dft;
}

// 강조 언어 선택 UI 에서 고를 수 있는 표준 언어 id 목록.
// 번들된 Prism 컴포넌트와 1:1 대응하며 web/preview.js 의 LANGS 와 동기화되어야 한다.
static const wchar_t *const kAllLangs[] = {
  L"bash", L"c", L"cpp", L"java", L"python", L"html", L"css", L"javascript", L"sql", L"json",
  L"typescript", L"yaml", L"go", L"rust", L"csharp", L"kotlin", L"markdown", L"diff", L"ini", L"toml"
};
static const int kAllLangsN = (int)(sizeof(kAllLangs) / sizeof(kAllLangs[0]));

enum {
  IDC_LANG_LIST   = 2007, // 활성 언어 리스트박스
  IDC_LANG_COMBO  = 2008, // 추가할 언어 콤보(select)
  IDC_LANG_REMOVE = 2009,
  IDC_LANG_ADD    = 2010,
};

// kAllLangs 내 정규 순서(없으면 맨 끝). 콤보를 항상 같은 순서로 유지하는 데 쓴다.
static int langRank(const wchar_t *s) {
  for (int i = 0; i < kAllLangsN; i++) if (lstrcmpiW(kAllLangs[i], s) == 0) return i;
  return kAllLangsN;
}
// id 를 콤보의 정규 순서 위치에 끼워 넣는다(제거된 언어를 되돌릴 때).
static void comboInsertCanonical(HWND combo, const wchar_t *id) {
  int rank = langRank(id);
  int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0), pos = n;
  for (int i = 0; i < n; i++) {
    wchar_t buf[64] = {};
    SendMessageW(combo, CB_GETLBTEXT, i, (LPARAM)buf);
    if (langRank(buf) > rank) { pos = i; break; }
  }
  SendMessageW(combo, CB_INSERTSTRING, pos, (LPARAM)id);
}

static bool g_setDlgDone, g_setDlgOk;
static LRESULT CALLBACK SettingsDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_COMMAND) {
    UINT id = LOWORD(w), code = HIWORD(w);
    if (id == IDOK)     { g_setDlgOk = true;  g_setDlgDone = true; return 0; }
    if (id == IDCANCEL) { g_setDlgOk = false; g_setDlgDone = true; return 0; }
    // 추가: 콤보에서 고른 언어를 활성 목록으로 옮긴다.
    if (id == IDC_LANG_ADD && code == BN_CLICKED) {
      HWND combo = GetDlgItem(h, IDC_LANG_COMBO), list = GetDlgItem(h, IDC_LANG_LIST);
      int ci = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
      if (ci >= 0) {
        wchar_t buf[64] = {};
        SendMessageW(combo, CB_GETLBTEXT, ci, (LPARAM)buf);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)buf);
        SendMessageW(combo, CB_DELETESTRING, ci, 0);
        int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        SendMessageW(combo, CB_SETCURSEL, n ? (ci < n ? ci : n - 1) : (WPARAM)-1, 0);
      }
      return 0;
    }
    // 제거: 선택한 활성 언어를 콤보로 되돌린다(버튼 또는 더블클릭).
    if ((id == IDC_LANG_REMOVE && code == BN_CLICKED) ||
        (id == IDC_LANG_LIST   && code == LBN_DBLCLK)) {
      HWND combo = GetDlgItem(h, IDC_LANG_COMBO), list = GetDlgItem(h, IDC_LANG_LIST);
      int li = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
      if (li >= 0) {
        wchar_t buf[64] = {};
        SendMessageW(list, LB_GETTEXT, li, (LPARAM)buf);
        SendMessageW(list, LB_DELETESTRING, li, 0);
        comboInsertCanonical(combo, buf);
        int n = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
        if (n) SendMessageW(list, LB_SETCURSEL, li < n ? li : n - 1, 0);
      }
      return 0;
    }
  } else if (m == WM_CLOSE) {
    g_setDlgOk = false; g_setDlgDone = true; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
static void showSettingsDialog() {
  static const char *kViews[] = { "editor", "split", "preview" };
  static const char *kThemes[] = { "system", "light", "dark" };
  static bool reg = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsDlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MyMDSettingsDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  int dw = 400, dh = 486;
  RECT pr; GetWindowRect(g_hwnd, &pr);
  int px = pr.left + ((pr.right - pr.left) - dw) / 2;
  int py = pr.top + ((pr.bottom - pr.top) - dh) / 2;
  HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
    L"MyMDSettingsDlg", W(u8"설정").c_str(),
    WS_POPUP | WS_CAPTION | WS_SYSMENU, px, py, dw, dh,
    g_hwnd, nullptr, hi, nullptr);
  if (!hDlg) return;

  auto mk = [&](const wchar_t *cls, const std::wstring &txt, DWORD style, int x, int y, int w2, int h2, int id) {
    HWND c = CreateWindowExW(0, cls, txt.c_str(), WS_CHILD | WS_VISIBLE | style,
      x, y, w2, h2, hDlg, (HMENU)(INT_PTR)id, hi, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return c;
  };
  const int lx = 20, cx = 150, cw = 210;
  mk(L"STATIC", W(u8"기본 보기"), SS_LEFT, lx, 22, 120, 18, -1);
  HWND cbView = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, cx, 18, cw, 200, 2001);
  for (auto s : { u8"편집만", u8"분할", u8"미리보기만" }) SendMessageW(cbView, CB_ADDSTRING, 0, (LPARAM)W(s).c_str());
  SendMessageW(cbView, CB_SETCURSEL, comboIndex(g_defaultView, kViews, 3, 1), 0);

  mk(L"STATIC", W(u8"테마"), SS_LEFT, lx, 56, 120, 18, -1);
  HWND cbTheme = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, cx, 52, cw, 200, 2002);
  for (auto s : { u8"시스템", u8"라이트", u8"다크" }) SendMessageW(cbTheme, CB_ADDSTRING, 0, (LPARAM)W(s).c_str());
  SendMessageW(cbTheme, CB_SETCURSEL, comboIndex(g_theme, kThemes, 3, 0), 0);

  mk(L"STATIC", W(u8"글꼴 크기 (10-32)"), SS_LEFT, lx, 90, 120, 18, -1);
  HWND eFont = mk(L"EDIT", std::to_wstring(g_fontSize), ES_NUMBER | WS_BORDER | WS_TABSTOP, cx, 86, 60, 24, 2003);

  mk(L"STATIC", W(u8"탭 크기 (1-8)"), SS_LEFT, lx, 124, 120, 18, -1);
  HWND eTab = mk(L"EDIT", std::to_wstring(g_tabSize), ES_NUMBER | WS_BORDER | WS_TABSTOP, cx, 120, 60, 24, 2004);

  HWND ckWrap = mk(L"BUTTON", W(u8"자동 줄바꿈"), BS_AUTOCHECKBOX | WS_TABSTOP, lx, 156, 200, 22, 2005);
  SendMessageW(ckWrap, BM_SETCHECK, g_wrap ? BST_CHECKED : BST_UNCHECKED, 0);
  HWND ckSync = mk(L"BUTTON", W(u8"스크롤 동기화"), BS_AUTOCHECKBOX | WS_TABSTOP, lx, 184, 200, 22, 2006);
  SendMessageW(ckSync, BM_SETCHECK, g_scrollSync ? BST_CHECKED : BST_UNCHECKED, 0);

  mk(L"STATIC", W(u8"강조 언어 (코드 블록 구문 강조)"), SS_LEFT, lx, 216, dw - 2*lx, 18, -1);
  HWND lbLangs = mk(L"LISTBOX", L"",
                    LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                    lx, 236, 250, 120, IDC_LANG_LIST);
  mk(L"BUTTON", W(u8"제거"), WS_TABSTOP, lx + 262, 236, 84, 26, IDC_LANG_REMOVE);

  HWND cbAddLang = mk(L"COMBOBOX", L"",
                      CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                      lx, 366, 250, 220, IDC_LANG_COMBO);
  mk(L"BUTTON", W(u8"추가"), WS_TABSTOP, lx + 262, 365, 84, 26, IDC_LANG_ADD);

  // 활성 언어 -> 리스트박스(저장 순서 보존), 미사용 지원 언어 -> 콤보(정규 순서).
  std::vector<std::string> enabledLangs = parseLangsJson(g_langsJson);
  for (auto &id : enabledLangs)
    SendMessageW(lbLangs, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(id).c_str());
  for (int i = 0; i < kAllLangsN; i++) {
    bool on = false;
    for (auto &id : enabledLangs)
      if (lstrcmpiW(utf8_to_wide(id).c_str(), kAllLangs[i]) == 0) { on = true; break; }
    if (!on) SendMessageW(cbAddLang, CB_ADDSTRING, 0, (LPARAM)kAllLangs[i]);
  }
  if (SendMessageW(cbAddLang, CB_GETCOUNT, 0, 0) > 0) SendMessageW(cbAddLang, CB_SETCURSEL, 0, 0);

  mk(L"BUTTON", W(u8"저장"), BS_DEFPUSHBUTTON | WS_TABSTOP, dw - 200, 410, 84, 28, IDOK);
  mk(L"BUTTON", W(u8"취소"), WS_TABSTOP, dw - 108, 410, 84, 28, IDCANCEL);

  SetFocus(cbView);
  EnableWindow(g_hwnd, FALSE);
  ShowWindow(hDlg, SW_SHOW);

  g_setDlgDone = false; g_setDlgOk = false;
  MSG msg;
  while (!g_setDlgDone) {
    BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { g_setDlgOk = false; break; }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
      HWND f = GetFocus();
      wchar_t cls[16] = {}; GetClassNameW(f, cls, 15);
      bool isCombo  = lstrcmpiW(cls, L"COMBOBOX") == 0 || lstrcmpiW(cls, L"ComboLBox") == 0;
      LONG bsty = (lstrcmpiW(cls, L"BUTTON") == 0) ? (GetWindowLongW(f, GWL_STYLE) & BS_TYPEMASK) : -1;
      bool isPush = (bsty == BS_PUSHBUTTON || bsty == BS_DEFPUSHBUTTON);
      if (isCombo) {
        // 콤보/드롭다운 목록의 Enter 는 선택 확정용 -> 통과
      } else if (isPush) {
        SendMessageW(f, BM_CLICK, 0, 0); // 포커스된 버튼(추가/제거/저장/취소) 실행
        continue;                        // 저장 여부는 해당 버튼의 WM_COMMAND 가 결정
      } else { g_setDlgOk = true; break; } // 그 외(편집/체크박스/리스트)는 저장
    }
    if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }

  if (g_setDlgOk) {
    std::string oldTheme = g_theme;
    std::string oldLangs = g_langsJson;
    bool oldWrap = g_wrap;

    int vi = (int)SendMessageW(cbView, CB_GETCURSEL, 0, 0);  if (vi < 0) vi = 1;
    int ti = (int)SendMessageW(cbTheme, CB_GETCURSEL, 0, 0); if (ti < 0) ti = 0;
    g_defaultView = kViews[vi];
    g_theme = kThemes[ti];
    g_fontSize = dlgClamp(dlgReadInt(eFont, g_fontSize), 10, 32);
    g_tabSize  = dlgClamp(dlgReadInt(eTab, g_tabSize), 1, 8);
    g_wrap = SendMessageW(ckWrap, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_scrollSync = SendMessageW(ckSync, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::string langsCsv;
    int langCount = (int)SendMessageW(lbLangs, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < langCount; i++) {
      wchar_t lb[64] = {};
      SendMessageW(lbLangs, LB_GETTEXT, i, (LPARAM)lb);
      if (i) langsCsv += ",";
      langsCsv += wide_to_utf8(lb);
    }
    g_langsJson = csvToLangsJson(langsCsv);

    // 런타임 적용
    applyEditStyle();                          // 글꼴/탭폭 즉시
    if (g_wrap != oldWrap) recreateEditForWrap();
    if (g_theme != oldTheme) {                 // 테마: 브러시 재생성 + 리페인트
      if (g_editBrush) DeleteObject(g_editBrush);
      g_editBrush = CreateSolidBrush(themeBg());
      InvalidateRect(g_edit, nullptr, TRUE);
      InvalidateRect(g_hwnd, nullptr, FALSE);
    }
    if (g_theme != oldTheme || g_langsJson != oldLangs) refreshPreview(); // 프리뷰 통지
    saveSettings();
  }
  EnableWindow(g_hwnd, TRUE);
  DestroyWindow(hDlg);
  SetForegroundWindow(g_hwnd);
  SetFocus(g_edit);
}

static void runBtn(int id) {
  switch (id) {
    case IDM_NEW:    doNew();    break;
    case IDM_OPEN:   doOpen();   break;
    case IDM_SAVE:   doSave();   break;
    case IDM_SAVEAS: doSaveAs(); break;
    case IDM_VSCODE: doOpenInVSCode(); break;
    case IDM_INSERTTABLE: { int c, r; if (showTableDialog(c, r)) insertTableSkeleton(c, r); break; }
    case IDM_FORMATTABLE: SetFocus(g_edit); doFormatTable(); break;
    case IDM_SETTINGS: showSettingsDialog(); break;
    case IDM_MIN:    ShowWindow(g_hwnd, SW_MINIMIZE); break;
    case IDM_MAX:    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case IDM_WCLOSE: SendMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    case IDM_VIEW_E: setView(0); break;
    case IDM_VIEW_S: setView(1); break;
    case IDM_VIEW_P: setView(2); break;
  }
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      createEdit(h);
      // 프리뷰 호스트 자식 창 (WebView2 는 지연 임베드, 초기엔 숨김)
      g_preview = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h,
                                  (HMENU)(INT_PTR)2, GetModuleHandleW(nullptr), nullptr);
      return 0;
    }
    case WM_NCCALCSIZE:
      if (w == TRUE) {
        if (IsZoomed(h)) { // 최대화 시 작업표시줄을 덮지 않게 프레임만큼 보정
          int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)l;
          p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
          p->rgrc[0].top += fy;  p->rgrc[0].bottom -= fy;
        }
        return 0; // 비클라이언트 제거 -> 전체가 클라이언트(프레임리스)
      }
      break;
    case WM_NCHITTEST: {
      RECT rc; GetWindowRect(h, &rc);
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      if (!IsZoomed(h)) {
        bool L_ = x < rc.left + kResizeBorder, R_ = x >= rc.right - kResizeBorder;
        bool T_ = y < rc.top + kResizeBorder,  B_ = y >= rc.bottom - kResizeBorder;
        if (T_ && L_) return HTTOPLEFT;   if (T_ && R_) return HTTOPRIGHT;
        if (B_ && L_) return HTBOTTOMLEFT; if (B_ && R_) return HTBOTTOMRIGHT;
        if (L_) return HTLEFT; if (R_) return HTRIGHT; if (T_) return HTTOP; if (B_) return HTBOTTOM;
      }
      POINT cp = { x, y }; ScreenToClient(h, &cp);
      if (cp.y < kTopbarH) return btnAt(cp.x, cp.y) >= 0 ? HTCLIENT : HTCAPTION;
      return HTCLIENT;
    }
    case WM_ERASEBKGND:
      return 1; // 깜빡임 방지: 상단바는 WM_PAINT, 나머지는 EDIT 가 그림
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
      RECT cr; GetClientRect(h, &cr);
      paintTopbar(dc, cr.right);
      if (g_view == 1 && g_dividerX >= 0) { // 디바이더 스트립
        RECT dv = { g_dividerX, kTopbarH, g_dividerX + kDividerW, cr.bottom };
        HBRUSH b = CreateSolidBrush(themeBorder()); FillRect(dc, &dv, b); DeleteObject(b);
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int mx = GET_X_LPARAM(l), my = GET_Y_LPARAM(l);
      if (g_view == 1 && g_dividerX >= 0 && mx >= g_dividerX && mx < g_dividerX + kDividerW && my >= kTopbarH) {
        SetCapture(h); g_divDrag = true; return 0;
      }
      int i = btnAt(mx, my);
      if (i >= 0) runBtn(g_btns[i].id);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (g_divDrag) {
        RECT cr; GetClientRect(h, &cr);
        double r = cr.right > 0 ? (double)GET_X_LPARAM(l) / (double)cr.right : 0.5;
        if (r < 0.12) r = 0.12;
        if (r > 0.88) r = 0.88;
        g_splitRatio = r; layout();
        return 0;
      }
      int i = btnAt(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      if (i != g_hotBtn) {
        g_hotBtn = i;
        invalidateTopbar();
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE; tme.hwndTrack = h;
        TrackMouseEvent(&tme);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      if (g_hotBtn != -1) { g_hotBtn = -1; invalidateTopbar(); }
      return 0;
    case WM_LBUTTONUP:
      if (g_divDrag) { g_divDrag = false; ReleaseCapture(); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT && g_view == 1 && g_dividerX >= 0) {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        if (p.x >= g_dividerX && p.x < g_dividerX + kDividerW && p.y >= kTopbarH) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
        }
      }
      break;
    case WM_TIMER:
      if (w == IDT_RENDER) { KillTimer(h, IDT_RENDER); pushPreviewNow(); }
      else if (w == IDT_SCROLLSYNC) { KillTimer(h, IDT_SCROLLSYNC); syncPreviewScroll(); }
      return 0;
    case WM_SIZE:
      layout();
      return 0;
    case WM_SETFOCUS:
      if (g_edit) SetFocus(g_edit);
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)w;
      SetTextColor(dc, themeFg());
      SetBkColor(dc, themeBg());
      return (LRESULT)g_editBrush;
    }
    case WM_COMMAND: {
      if ((HWND)l == g_edit && HIWORD(w) == EN_CHANGE) {
        if (!g_suppressDirty) {
          if (!g_dirty) { g_dirty = true; updateTitle(); }
          if (g_view != 0) SetTimer(h, IDT_RENDER, 120, nullptr); // 프리뷰 디바운스
        }
        return 0;
      }
      switch (LOWORD(w)) {
        case IDM_NEW:    doNew();    return 0;
        case IDM_OPEN:   doOpen();   return 0;
        case IDM_SAVE:   doSave();   return 0;
        case IDM_SAVEAS: doSaveAs(); return 0;
        case IDM_VSCODE: doOpenInVSCode(); return 0;
        case IDM_INSERTTABLE: runBtn(IDM_INSERTTABLE); return 0;
        case IDM_FORMATTABLE: runBtn(IDM_FORMATTABLE); return 0;
        case IDM_SETTINGS:    runBtn(IDM_SETTINGS);    return 0;
      }
      break;
    }
    case WM_GETMINMAXINFO: {
      MINMAXINFO *mmi = (MINMAXINFO *)l;
      mmi->ptMinTrackSize.x = 480;
      mmi->ptMinTrackSize.y = 320;
      return 0;
    }
    case WM_CLOSE:
      if (g_dirty) {
        int r = MessageBoxW(h,
            W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x80 \xEB\xB3\x80\xEA\xB2\xBD \xEC\x82\xAC\xED\x95\xAD\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\x8B\x9C\xEA\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?").c_str(),
            L"MyMD", MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return 0;
        if (r == IDYES && !doSave()) return 0; // 저장 실패/취소 시 닫지 않음
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------
int main() {
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    if (argc > 1 && argv[1] && argv[1][0]) g_pendingOpen = argv[1];
    LocalFree(argv);
  }

  loadSettings();
  g_view = (g_defaultView == "editor") ? 0 : (g_defaultView == "preview") ? 2 : 1;
  g_editBrush = CreateSolidBrush(themeBg());
  g_uiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
  g_glyphFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

  HINSTANCE hInst = GetModuleHandleW(nullptr);
  HICON hIcon   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  HICON hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = MainProc;
  wc.hInstance = hInst;
  wc.lpszClassName = L"MyMDMain";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = g_editBrush;
  wc.hIcon = hIcon;
  wc.hIconSm = hIconSm;
  RegisterClassExW(&wc);

  g_hwnd = CreateWindowExW(0, L"MyMDMain", L"MyMD",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1120, 740,
                           nullptr, nullptr, hInst, nullptr);
  // 프레임리스 적용 (WM_NCCALCSIZE 가 비클라이언트를 제거하도록 프레임 변경 통지)
  SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

  // 실행 인자 파일 로드 (창 표시 전)
  if (!g_pendingOpen.empty()) {
    std::string content;
    if (readFile(g_pendingOpen, content)) {
      setCurrentFile(g_pendingOpen);
      setEditText(content);
      g_dirty = false;
    }
  }
  updateTitle();

  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);          // 에디터 즉시 페인트
  SetFocus(g_edit);
  setView(g_view);               // 분할/미리보기면 여기서 프리뷰 엔진 지연 생성

  ACCEL accels[] = {
    { FCONTROL | FVIRTKEY, 'N', IDM_NEW },
    { FCONTROL | FVIRTKEY, 'O', IDM_OPEN },
    { FCONTROL | FVIRTKEY, 'S', IDM_SAVE },
    { FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVEAS },
    { FCONTROL | FVIRTKEY, '1', IDM_VIEW_E },
    { FCONTROL | FVIRTKEY, '2', IDM_VIEW_S },
    { FCONTROL | FVIRTKEY, '3', IDM_VIEW_P },
    { FCONTROL | FSHIFT | FVIRTKEY, 'V', IDM_VSCODE },
    { FCONTROL | FVIRTKEY, 'T', IDM_INSERTTABLE },
    { FCONTROL | FSHIFT | FVIRTKEY, 'F', IDM_FORMATTABLE },
    { FCONTROL | FVIRTKEY, VK_OEM_COMMA, IDM_SETTINGS },
  };
  HACCEL hAccel = CreateAcceleratorTableW(accels, (int)(sizeof(accels) / sizeof(accels[0])));

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(g_hwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
