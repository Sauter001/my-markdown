// 프리뷰: WebView2 호스트 자식 창 + 엔진(지연 생성) 소유. JS 브리지 프로토콜
// 캡슐화.
//   webview.h 는 구현부(.cpp)에만 포함해 다른 번역 단위로 누출시키지 않는다.
#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace webview {
class webview;
}  // namespace webview

class Preview {
 public:
  Preview();  // unique_ptr<incomplete> 생성/소멸을 .cpp 로 한정
  ~Preview();

  void createHost(HWND parent);  // STATIC 호스트 자식 창 생성
  HWND host() const { return host_; }
  void ensureEngine(
      HWND focusBack);   // WebView2 지연 생성 + 바인드 + 내비게이트
  void destroyEngine();  // 호스트 창 살아있을 때 컨트롤러 정리
  bool ready() const { return ready_; }
  void updateBounds();

  void pushRender(const std::string& utf8lf);
  void pushConfig(bool dark, const std::string& langsJson,
                  const std::wstring& curDir, int zoom);
  void pushKeymap(
      const std::string& keymapJson);  // 미리보기 포커스 단축키 매핑
  void pushZoom(int zoom);
  void scrollTo(double ratio);

  std::function<void()> onReady;  // 준비 완료 시 (App: config+render)
  std::function<std::string(const std::string& req)>
      onOpenExternal;  // 외부 링크 (App: ShellExecute)
  std::function<void(const std::string& id)>
      onAccel;  // 미리보기 포커스 단축키 (App: 명령 디스패치)

 private:
  HWND host_ = nullptr;
  std::unique_ptr<webview::webview> webview_;
  bool ready_ = false;
};
