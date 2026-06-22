// App: 앱 전체 상태와 동작의 소유자 겸 조립자(mediator).
//   컴포넌트(Settings/Theme/Topbar/Editor/TableEditor/Preview/대화상자)를
//   소유하고, 메인 창 프로시저를 정적 thunk(GWLP_USERDATA)로 인스턴스에
//   위임하며, 컴포넌트 간 통지를 std::function 콜백으로 배선한다.
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "core/raii.h"
#include "model/Settings.h"
#include "model/Theme.h"
#include "ui/Editor.h"
#include "ui/Preview.h"
#include "ui/TableEditor.h"
#include "ui/Topbar.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/TableDialog.h"

// 명령(액션) 한 항목: 명령 ID 와 실행 핸들러. 상단바 버튼/액셀러레이터/
// 미리보기 단축키가 모두 같은 ID 로 이 레지스트리를 통해 디스패치된다.
struct Command {
  int id;
  std::function<void()> run;
};

class App {
 public:
  int run();
  static LRESULT CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);

 private:
  LRESULT onMessage(HWND, UINT, WPARAM, LPARAM);
  void wireCallbacks();

  // 문서/파일
  void setCurrentFile(const std::wstring& path);
  void updateTitle();
  bool openDialog(std::wstring& outPath);
  bool saveDialog(const std::wstring& suggested, std::wstring& outPath);
  bool confirmDiscard();
  void newFile();
  void openFile();
  bool saveFile();
  bool saveFileAs();
  void openInVSCode();
  bool launchVSCode(const std::wstring& file);
  std::string openExternal(const std::string& req);

  // 보기/레이아웃/프리뷰
  void invalidateTopbar();
  void layout();
  void setView(int v);
  void refreshPreview();
  void pushPreviewConfigAndRender();
  void requestScrollSync();
  void syncPreviewScroll();

  // 설정/줌
  void setZoom(int z);
  void applySettingsChange(const Settings& before);

  // DPI
  int dpiScale(int px96) const;  // 96dpi 논리 픽셀 -> 현재 dpi_ 실제 픽셀
  void makeUiFonts();            // dpi_ 기준으로 uiFont_/glyphFont_ 생성

  // 단축키
  void rebuildAccel();  // settings_.keymapJson 으로 ACCEL 테이블 재생성

  // 명령
  void buildCommands();  // 명령 레지스트리(id -> 핸들러) 1회 구성
  void runBtn(int id);   // id 로 명령을 찾아 실행(없으면 무시)

  // 소유 컴포넌트
  Settings settings_;
  Theme theme_;
  Topbar topbar_;
  Editor editor_;
  TableEditor tableEditor_;
  Preview preview_;
  TableDialog tableDialog_;
  SettingsDialog settingsDialog_;

  // 창/리소스
  std::vector<Command> commands_;  // 명령 레지스트리(buildCommands 로 구성)

  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;            // 현재 창 DPI (WM_DPICHANGED 로 갱신)
  HACCEL hAccel_ = nullptr;  // 키맵 기반 액셀러레이터(설정 변경 시 재생성)
  FontHandle uiFont_;
  FontHandle glyphFont_;
  BrushHandle editBrush_;

  // 문서/보기 상태
  std::wstring curPath_, curName_, curDir_, pendingOpen_;
  bool dirty_ = false;
  int view_ = 1;             // 0 에디터, 1 분할, 2 미리보기
  double splitRatio_ = 0.5;  // 분할 보기 에디터 비율
  bool divDrag_ = false;     // 디바이더 드래그 중
  int dividerX_ = -1;        // 디바이더 좌표 (분할 보기, 아니면 -1)
};
