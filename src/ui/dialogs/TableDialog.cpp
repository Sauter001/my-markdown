#include "ui/dialogs/TableDialog.h"

#include "core/dpi.h"
#include "core/str_util.h"
#include "ui/dialogs/dlg_util.h"

LRESULT CALLBACK TableDialog::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE)
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  TableDialog* self = (TableDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self && m == WM_COMMAND) {
    if (LOWORD(w) == IDOK) {
      self->ok_ = true;
      self->done_ = true;
      return 0;
    }
    if (LOWORD(w) == IDCANCEL) {
      self->ok_ = false;
      self->done_ = true;
      return 0;
    }
  } else if (self && m == WM_CLOSE) {
    self->ok_ = false;
    self->done_ = true;
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

bool TableDialog::show(HWND parent, HFONT uiFont, int& cols, int& rows) {
  static bool reg = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TableDialog::proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MyMDTableDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  UINT dp =
      dpi::forWindow(parent);  // 좌표는 96dpi 논리값, 생성 시 dp 로 스케일
  int dw = 230, dh = 150;
  int dwp = dpi::scale(dw, dp), dhp = dpi::scale(dh, dp);
  RECT pr;
  GetWindowRect(parent, &pr);
  int px = pr.left + ((pr.right - pr.left) - dwp) / 2;
  int py = pr.top + ((pr.bottom - pr.top) - dhp) / 2;
  HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                              L"MyMDTableDlg", W(u8"표 삽입").c_str(),
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
  mk(L"STATIC", W(u8"열 수"), SS_RIGHT, 14, 20, 44, 18, -1);
  HWND eCols =
      mk(L"EDIT", L"2", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 66,
         18, 56, 24, 1001);
  mk(L"STATIC", W(u8"행 수"), SS_RIGHT, 14, 52, 44, 18, -1);
  HWND eRows =
      mk(L"EDIT", L"2", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 66,
         50, 56, 24, 1002);
  mk(L"BUTTON", W(u8"삽입"), BS_DEFPUSHBUTTON | WS_TABSTOP, 138, 18, 76, 26,
     IDOK);
  mk(L"BUTTON", W(u8"취소"), WS_TABSTOP, 138, 50, 76, 26, IDCANCEL);

  SetFocus(eCols);
  SendMessageW(eCols, EM_SETSEL, 0, -1);
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
    }  // 앱 종료 전파
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
      ok_ = true;
      break;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
      ok_ = false;
      break;
    }
    if (!IsDialogMessageW(hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (ok_) {
    cols = dlgClamp(dlgReadInt(eCols, 2), 1, 20);
    rows = dlgClamp(dlgReadInt(eRows, 2), 1, 50);
  }
  EnableWindow(parent, TRUE);
  DestroyWindow(hDlg);
  SetForegroundWindow(parent);
  return ok_;
}
