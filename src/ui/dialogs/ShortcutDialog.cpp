#include "ui/dialogs/ShortcutDialog.h"

#include <utility>

#include "core/dpi.h"
#include "core/str_util.h"
#include "model/Keymap.h"

enum { IDC_LIST = 101, IDC_RESET = 102 };

LRESULT CALLBACK ShortcutDialog::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE)
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  ShortcutDialog* self = (ShortcutDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self && m == WM_COMMAND) {
    WORD id = LOWORD(w);
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
    if (id == IDC_RESET) {
      self->resetAll();
      return 0;
    }
  } else if (self && m == WM_CLOSE) {
    self->ok_ = false;
    self->done_ = true;
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void ShortcutDialog::refresh() {
  int sel = (int)SendMessageW(lb_, LB_GETCURSEL, 0, 0);
  SendMessageW(lb_, WM_SETREDRAW, FALSE, 0);
  SendMessageW(lb_, LB_RESETCONTENT, 0, 0);
  const auto& acts = keymap::actions();
  for (size_t i = 0; i < acts.size(); i++) {
    std::wstring keyTxt =
        specs_[i].empty() ? W(u8"없음") : utf8_to_wide(specs_[i]);
    std::wstring line = utf8_to_wide(acts[i].label) + L"   (" + keyTxt + L")";
    SendMessageW(lb_, LB_ADDSTRING, 0, (LPARAM)line.c_str());
  }
  if (sel >= 0) SendMessageW(lb_, LB_SETCURSEL, sel, 0);
  SendMessageW(lb_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(lb_, nullptr, TRUE);
}

void ShortcutDialog::resetAll() {
  const auto& acts = keymap::actions();
  for (size_t i = 0; i < acts.size(); i++) specs_[i] = acts[i].def;
  refresh();
}

bool ShortcutDialog::show(HWND parent, HFONT uiFont, std::string& keymapJson) {
  static bool reg = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ShortcutDialog::proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MyMDShortcutDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  const auto& acts = keymap::actions();
  specs_.clear();
  for (const auto& a : acts)
    specs_.push_back(keymap::currentSpec(a, keymapJson));

  UINT dp = dpi::forWindow(parent);
  int dw = 380, dh = 470;
  int dwp = dpi::scale(dw, dp), dhp = dpi::scale(dh, dp);
  RECT pr;
  GetWindowRect(parent, &pr);
  int px = pr.left + ((pr.right - pr.left) - dwp) / 2;
  int py = pr.top + ((pr.bottom - pr.top) - dhp) / 2;
  HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                              L"MyMDShortcutDlg", W(u8"단축키 설정").c_str(),
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
  const int lx = 16;
  mk(L"STATIC",
     W(u8"동작을 선택하고 새 단축키를 누르세요. (Backspace: 기본값)"), SS_LEFT,
     lx, 12, dw - 2 * lx, 18, -1);
  lb_ = mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
           lx, 36, dw - 2 * lx, 348, IDC_LIST);
  mk(L"BUTTON", W(u8"기본값 복원"), WS_TABSTOP, lx, 396, 120, 28, IDC_RESET);
  mk(L"BUTTON", W(u8"저장"), BS_DEFPUSHBUTTON | WS_TABSTOP, dw - 200, 396, 84,
     28, IDOK);
  mk(L"BUTTON", W(u8"취소"), WS_TABSTOP, dw - 108, 396, 84, 28, IDCANCEL);

  refresh();
  SendMessageW(lb_, LB_SETCURSEL, 0, 0);
  SetFocus(lb_);
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
    if (msg.message == WM_KEYDOWN) {
      int vk = (int)msg.wParam;
      if (vk == VK_ESCAPE) {
        ok_ = false;
        break;
      }
      bool isMod = (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
                    vk == VK_LWIN || vk == VK_RWIN);
      if (!isMod) {
        int sel = (int)SendMessageW(lb_, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && (vk == VK_BACK || vk == VK_DELETE)) {
          specs_[sel] = acts[sel].def;
          refresh();
          continue;  // 기본값 복원
        }
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (sel >= 0 && (ctrl || alt)) {
          std::string spec = keymap::formatSpec(ctrl, shift, alt, (WORD)vk);
          if (!spec.empty()) {
            // 같은 조합을 쓰던 다른 동작은 비워(없음) 충돌을 막는다.
            for (size_t i = 0; i < specs_.size(); i++)
              if ((int)i != sel && specs_[i] == spec) specs_[i].clear();
            specs_[sel] = spec;
            refresh();
            continue;
          }
        }
      }
    }
    if (!IsDialogMessageW(hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (ok_) {
    std::vector<std::pair<std::string, std::string>> ov;
    for (size_t i = 0; i < acts.size(); i++)
      if (specs_[i] != acts[i].def) ov.push_back({acts[i].id, specs_[i]});
    keymapJson = keymap::toJson(ov);
  }
  EnableWindow(parent, TRUE);
  DestroyWindow(hDlg);
  SetForegroundWindow(parent);
  return ok_;
}
