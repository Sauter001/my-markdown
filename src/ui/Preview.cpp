#include "ui/Preview.h"

#include <cstdio>

#include "core/file_io.h"
#include "core/str_util.h"
#include "webview.h"

// webview 완전 타입이 보이는 TU 에서만 생성/소멸 (다른 TU 로 누출 방지)
Preview::Preview() = default;
Preview::~Preview() = default;

void Preview::createHost(HWND parent) {
  host_ =
      CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, parent,
                      (HMENU)(INT_PTR)2, GetModuleHandleW(nullptr), nullptr);
}

void Preview::ensureEngine(HWND focusBack) {
  if (webview_) return;
  webview_ = std::make_unique<webview::webview>(
      false, (void*)&host_);  // 외부 창(우측 패널)에 임베드
  webview_->bind("mymdPreviewReady", [this](std::string) {
    ready_ = true;
    if (onReady) onReady();
    return std::string("true");
  });
  webview_->bind("mymdOpenExternal", [this](std::string r) {
    return onOpenExternal ? onOpenExternal(r) : std::string("{\"ok\":false}");
  });
  webview_->bind("mymdAccel", [this](std::string r) {
    if (onAccel) onAccel(firstStringArg(r));  // 미리보기에서 눌린 앱 단축키 id
    return std::string("true");
  });
  webview_->navigate(toFileUrl(exeDir() + L"\\web\\preview.html"));
  SetFocus(focusBack);  // 생성 시 프리뷰로 간 포커스 복귀
}

void Preview::destroyEngine() { webview_.reset(); }
void Preview::updateBounds() {
  if (webview_) webview_->update_bounds();
}

void Preview::pushRender(const std::string& utf8lf) {
  if (!webview_ || !ready_) return;
  webview_->eval("window.mymdRender&&window.mymdRender(\"" +
                 base64_encode(utf8lf) + "\")");
}
void Preview::pushConfig(bool dark, const std::string& langsJson,
                         const std::wstring& curDir, int zoom) {
  if (!webview_ || !ready_) return;
  webview_->eval(std::string("window.mymdSetTheme&&window.mymdSetTheme(\"") +
                 (dark ? "dark" : "light") + "\")");
  webview_->eval("window.mymdSetLangs&&window.mymdSetLangs(\"" +
                 base64_encode(langsJson) + "\")");
  std::string base = curDir.empty() ? std::string() : (toFileUrl(curDir) + "/");
  webview_->eval("window.mymdSetBase&&window.mymdSetBase(\"" +
                 base64_encode(base) + "\")");
  webview_->eval("window.mymdSetZoom&&window.mymdSetZoom(" +
                 std::to_string(zoom) + ")");
}
void Preview::pushKeymap(const std::string& keymapJson) {
  if (!webview_ || !ready_) return;
  webview_->eval("window.mymdSetKeymap&&window.mymdSetKeymap(\"" +
                 base64_encode(keymapJson) + "\")");
}
void Preview::pushZoom(int zoom) {
  if (!webview_ || !ready_) return;
  webview_->eval("window.mymdSetZoom&&window.mymdSetZoom(" +
                 std::to_string(zoom) + ")");
}
void Preview::scrollTo(double ratio) {
  if (!webview_ || !ready_) return;
  char buf[64];
  snprintf(buf, sizeof(buf), "%.5f", ratio);
  webview_->eval(std::string("window.mymdScrollTo&&window.mymdScrollTo(") +
                 buf + ")");
}
