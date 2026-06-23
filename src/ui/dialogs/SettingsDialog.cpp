#include "ui/dialogs/SettingsDialog.h"

#include <string>
#include <vector>

#include "core/dpi.h"
#include "core/json.h"
#include "core/str_util.h"
#include "model/Settings.h"
#include "ui/dialogs/ShortcutDialog.h"
#include "ui/dialogs/dlg_util.h"

static int comboIndex(const std::string& v, const char* const* opts, int n,
                      int dft) {
  for (int i = 0; i < n; i++)
    if (v == opts[i]) return i;
  return dft;
}

// 강조 언어 선택 UI 후보. 번들된 Prism 컴포넌트와 1:1 대응하며
// web/preview.js 의 LANGS 와 동기화되어야 한다.
static const wchar_t* const kAllLangs[] = {
    L"bash",       L"c",        L"cpp",        L"java", L"python",
    L"html",       L"css",      L"javascript", L"sql",  L"json",
    L"typescript", L"yaml",     L"go",         L"rust", L"csharp",
    L"kotlin",     L"markdown", L"diff",       L"ini",  L"toml"};
static const int kAllLangsN = (int)(sizeof(kAllLangs) / sizeof(kAllLangs[0]));

enum {
  IDC_LANG_LIST = 2007,   // 활성 언어 리스트박스
  IDC_LANG_COMBO = 2008,  // 추가할 언어 콤보(select)
  IDC_LANG_REMOVE = 2009,
  IDC_LANG_ADD = 2010,
  IDC_SHORTCUTS = 2011,  // 단축키 설정 열기
};

// kAllLangs 내 정규 순서(없으면 맨 끝). 콤보를 항상 같은 순서로 유지하는 데
// 쓴다.
static int langRank(const wchar_t* s) {
  for (int i = 0; i < kAllLangsN; i++)
    if (lstrcmpiW(kAllLangs[i], s) == 0) return i;
  return kAllLangsN;
}
// id 를 콤보의 정규 순서 위치에 끼워 넣는다(제거된 언어를 되돌릴 때).
static void comboInsertCanonical(HWND combo, const wchar_t* id) {
  int rank = langRank(id);
  int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0), pos = n;
  for (int i = 0; i < n; i++) {
    wchar_t buf[64] = {};
    SendMessageW(combo, CB_GETLBTEXT, i, (LPARAM)buf);
    if (langRank(buf) > rank) {
      pos = i;
      break;
    }
  }
  SendMessageW(combo, CB_INSERTSTRING, pos, (LPARAM)id);
}

LRESULT CALLBACK SettingsDialog::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE)
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  SettingsDialog* self = (SettingsDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self && m == WM_COMMAND) {
    UINT id = LOWORD(w), code = HIWORD(w);
    if (id == IDOK) {
      self->ok_ = true;
      self->done_ = true;
      return 0;
    }
    if (id == IDCANCEL) {
      self->ok_ = false;
      self->done_ = true;
      return 0;
    }
    // 단축키 설정 하위 대화상자(작업본 keymapWork_ 편집, 설정 저장 시 반영).
    if (id == IDC_SHORTCUTS && code == BN_CLICKED) {
      ShortcutDialog sd;
      sd.show(h, self->uiFont_, self->keymapWork_);
      return 0;
    }
    // 추가: 콤보에서 고른 언어를 활성 목록으로 옮긴다.
    if (id == IDC_LANG_ADD && code == BN_CLICKED) {
      HWND combo = GetDlgItem(h, IDC_LANG_COMBO),
           list = GetDlgItem(h, IDC_LANG_LIST);
      int ci = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
      if (ci >= 0) {
        wchar_t buf[64] = {};
        SendMessageW(combo, CB_GETLBTEXT, ci, (LPARAM)buf);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)buf);
        SendMessageW(combo, CB_DELETESTRING, ci, 0);
        int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        SendMessageW(combo, CB_SETCURSEL,
                     n ? (ci < n ? ci : n - 1) : (WPARAM)-1, 0);
      }
      return 0;
    }
    // 제거: 선택한 활성 언어를 콤보로 되돌린다(버튼 또는 더블클릭).
    if ((id == IDC_LANG_REMOVE && code == BN_CLICKED) ||
        (id == IDC_LANG_LIST && code == LBN_DBLCLK)) {
      HWND combo = GetDlgItem(h, IDC_LANG_COMBO),
           list = GetDlgItem(h, IDC_LANG_LIST);
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
  } else if (self && m == WM_CLOSE) {
    self->ok_ = false;
    self->done_ = true;
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

bool SettingsDialog::show(HWND parent, HFONT uiFont, Settings& s) {
  static const char* kViews[] = {"editor", "split", "preview"};
  static const char* kThemes[] = {"system", "light", "dark"};
  uiFont_ = uiFont;
  keymapWork_ = s.keymapJson;  // 단축키 작업본(하위 대화상자가 편집)
  static bool reg = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsDialog::proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MyMDSettingsDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  UINT dp =
      dpi::forWindow(parent);  // 좌표는 96dpi 논리값, 생성 시 dp 로 스케일
  int dw = 400, dh = 570;      // 논리 크기
  int dwp = dpi::scale(dw, dp), dhp = dpi::scale(dh, dp);
  RECT pr;
  GetWindowRect(parent, &pr);
  int px = pr.left + ((pr.right - pr.left) - dwp) / 2;
  int py = pr.top + ((pr.bottom - pr.top) - dhp) / 2;
  HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                              L"MyMDSettingsDlg", W(u8"설정").c_str(),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, px, py, dwp,
                              dhp, parent, nullptr, hi, this);
  if (!hDlg) return false;

  auto mk = [&](const wchar_t* cls, const std::wstring& txt, DWORD style, int x,
                int y, int w2, int h2, int id) {
    HWND c = CreateWindowExW(0, cls, txt.c_str(), WS_CHILD | WS_VISIBLE | style,
                             dpi::scale(x, dp), dpi::scale(y, dp),
                             dpi::scale(w2, dp), dpi::scale(h2, dp), hDlg,
                             (HMENU)(INT_PTR)id, hi, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)uiFont, TRUE);
    return c;
  };
  const int lx = 20, cx = 150, cw = 210;
  mk(L"STATIC", W(u8"기본 보기"), SS_LEFT, lx, 22, 120, 18, -1);
  HWND cbView = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                   cx, 18, cw, 200, 2001);
  for (auto v : {u8"편집만", u8"분할", u8"미리보기만"})
    SendMessageW(cbView, CB_ADDSTRING, 0, (LPARAM)W(v).c_str());
  SendMessageW(cbView, CB_SETCURSEL, comboIndex(s.defaultView, kViews, 3, 1),
               0);

  mk(L"STATIC", W(u8"테마"), SS_LEFT, lx, 56, 120, 18, -1);
  HWND cbTheme =
      mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, cx, 52,
         cw, 200, 2002);
  for (auto v : {u8"시스템", u8"라이트", u8"다크"})
    SendMessageW(cbTheme, CB_ADDSTRING, 0, (LPARAM)W(v).c_str());
  SendMessageW(cbTheme, CB_SETCURSEL, comboIndex(s.theme, kThemes, 3, 0), 0);

  mk(L"STATIC", W(u8"글꼴 크기 pt (6-40)"), SS_LEFT, lx, 90, 120, 18, -1);
  HWND eFont = mk(L"EDIT", std::to_wstring(s.fontSize),
                  ES_NUMBER | WS_BORDER | WS_TABSTOP, cx, 86, 60, 24, 2003);

  mk(L"STATIC", W(u8"탭 크기 (1-8)"), SS_LEFT, lx, 124, 120, 18, -1);
  HWND eTab = mk(L"EDIT", std::to_wstring(s.tabSize),
                 ES_NUMBER | WS_BORDER | WS_TABSTOP, cx, 120, 60, 24, 2004);

  HWND ckWrap = mk(L"BUTTON", W(u8"자동 줄바꿈"), BS_AUTOCHECKBOX | WS_TABSTOP,
                   lx, 156, 200, 22, 2005);
  SendMessageW(ckWrap, BM_SETCHECK, s.wrap ? BST_CHECKED : BST_UNCHECKED, 0);
  HWND ckSync = mk(L"BUTTON", W(u8"스크롤 동기화"),
                   BS_AUTOCHECKBOX | WS_TABSTOP, lx, 184, 200, 22, 2006);
  SendMessageW(ckSync, BM_SETCHECK, s.scrollSync ? BST_CHECKED : BST_UNCHECKED,
               0);
  HWND ckAuto = mk(L"BUTTON", W(u8"자동 페어링 (괄호/따옴표/백틱)"),
                   BS_AUTOCHECKBOX | WS_TABSTOP, lx, 212, 280, 22, 2012);
  SendMessageW(ckAuto, BM_SETCHECK, s.autoPair ? BST_CHECKED : BST_UNCHECKED, 0);

  HWND ckSmooth = mk(L"BUTTON", W(u8"부드러운 스크롤 (애니메이션)"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, lx, 240, 280, 22, 2013);
  SendMessageW(ckSmooth, BM_SETCHECK,
               s.smoothScroll ? BST_CHECKED : BST_UNCHECKED, 0);

  mk(L"STATIC", W(u8"스크롤 줄 수 (1-15)"), SS_LEFT, lx, 272, 120, 18, -1);
  HWND eScrollLines = mk(L"EDIT", std::to_wstring(s.scrollLines),
                         ES_NUMBER | WS_BORDER | WS_TABSTOP, cx, 268, 60, 24,
                         2014);

  mk(L"STATIC", W(u8"강조 언어 (코드 블록 구문 강조)"), SS_LEFT, lx, 300,
     dw - 2 * lx, 18, -1);
  HWND lbLangs =
      mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, lx,
         320, 250, 120, IDC_LANG_LIST);
  mk(L"BUTTON", W(u8"제거"), WS_TABSTOP, lx + 262, 320, 84, 26,
     IDC_LANG_REMOVE);

  HWND cbAddLang =
      mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, lx, 450,
         250, 220, IDC_LANG_COMBO);
  mk(L"BUTTON", W(u8"추가"), WS_TABSTOP, lx + 262, 449, 84, 26, IDC_LANG_ADD);

  // 활성 언어 -> 리스트박스(저장 순서 보존), 미사용 지원 언어 -> 콤보(정규
  // 순서).
  const std::vector<std::string>& enabledLangs = s.highlightLanguages;
  for (auto& id : enabledLangs)
    SendMessageW(lbLangs, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(id).c_str());
  for (int i = 0; i < kAllLangsN; i++) {
    bool on = false;
    for (auto& id : enabledLangs)
      if (lstrcmpiW(utf8_to_wide(id).c_str(), kAllLangs[i]) == 0) {
        on = true;
        break;
      }
    if (!on) SendMessageW(cbAddLang, CB_ADDSTRING, 0, (LPARAM)kAllLangs[i]);
  }
  if (SendMessageW(cbAddLang, CB_GETCOUNT, 0, 0) > 0)
    SendMessageW(cbAddLang, CB_SETCURSEL, 0, 0);

  mk(L"BUTTON", W(u8"단축키..."), WS_TABSTOP, lx, 494, 110, 28, IDC_SHORTCUTS);
  mk(L"BUTTON", W(u8"저장"), BS_DEFPUSHBUTTON | WS_TABSTOP, dw - 200, 494, 84,
     28, IDOK);
  mk(L"BUTTON", W(u8"취소"), WS_TABSTOP, dw - 108, 494, 84, 28, IDCANCEL);

  SetFocus(cbView);
  EnableWindow(parent, FALSE);
  ShowWindow(hDlg, SW_SHOW);

  done_ = false;
  ok_ = false;
  MSG msg;
  while (!done_) {
    BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r == 0) {
      PostQuitMessage((int)msg.wParam);
      break;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
      ok_ = false;
      break;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
      HWND f = GetFocus();
      wchar_t cls[16] = {};
      GetClassNameW(f, cls, 15);
      bool isCombo =
          lstrcmpiW(cls, L"COMBOBOX") == 0 || lstrcmpiW(cls, L"ComboLBox") == 0;
      LONG bsty = (lstrcmpiW(cls, L"BUTTON") == 0)
                      ? (GetWindowLongW(f, GWL_STYLE) & BS_TYPEMASK)
                      : -1;
      bool isPush = (bsty == BS_PUSHBUTTON || bsty == BS_DEFPUSHBUTTON);
      if (isCombo) {
        // 콤보/드롭다운 목록의 Enter 는 선택 확정용 -> 통과
      } else if (isPush) {
        SendMessageW(f, BM_CLICK, 0,
                     0);  // 포커스된 버튼(추가/제거/저장/취소) 실행
        continue;         // 저장 여부는 해당 버튼의 WM_COMMAND 가 결정
      } else {
        ok_ = true;
        break;
      }  // 그 외(편집/체크박스/리스트)는 저장
    }
    if (!IsDialogMessageW(hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (ok_) {
    int vi = (int)SendMessageW(cbView, CB_GETCURSEL, 0, 0);
    if (vi < 0) vi = 1;
    int ti = (int)SendMessageW(cbTheme, CB_GETCURSEL, 0, 0);
    if (ti < 0) ti = 0;
    s.defaultView = kViews[vi];
    s.theme = kThemes[ti];
    s.fontSize = dlgClamp(dlgReadInt(eFont, s.fontSize), 6, 40);  // pt
    s.tabSize = dlgClamp(dlgReadInt(eTab, s.tabSize), 1, 8);
    s.wrap = SendMessageW(ckWrap, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.scrollSync = SendMessageW(ckSync, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.smoothScroll = SendMessageW(ckSmooth, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.scrollLines = dlgClamp(dlgReadInt(eScrollLines, s.scrollLines), 1, 15);
    s.autoPair = SendMessageW(ckAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.keymapJson = keymapWork_;  // 단축키 작업본 반영
    std::vector<std::string> langs;
    int langCount = (int)SendMessageW(lbLangs, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < langCount; i++) {
      wchar_t lb[64] = {};
      SendMessageW(lbLangs, LB_GETTEXT, i, (LPARAM)lb);
      langs.push_back(wide_to_utf8(lb));
    }
    s.highlightLanguages = langs;
  }
  EnableWindow(parent, TRUE);
  DestroyWindow(hDlg);
  SetForegroundWindow(parent);
  return ok_;
}
