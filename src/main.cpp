// MyMD - 간이 마크다운 에디터 (네이티브 Win32)
//
// 진입점. 실제 구현은 책임별 모듈로 분리되어 있다(A2 리팩토링):
//   core/   인코딩, JSON, 파일 입출력, 마크다운(리스트/표) 순수 로직, RAII
//   model/  Settings(설정), Theme(테마 색)
//   ui/     Editor, TableEditor, Topbar, Preview, dialogs/*
//   App     위 컴포넌트 조립 + 메인 윈도우 프로시저 + 콜백 배선
#include "App.h"

int main() {
  App app;
  return app.run();
}
