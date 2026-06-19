# Phase 2 (에디터 네이티브 분리) 남은 작업

브랜치: `feature/editor-separation`. 전체 설계는 `proto/PHASE1_FINDINGS.md`와 승인된 계획 참고.

## 현재 상태 (완료)

(커밋 해시는 리베이스로 바뀔 수 있어 적지 않음. `git log --oneline`의 `feat(editor-separation)` 참고.)

- 2a 네이티브 EDIT 에디터 셸
- 2b 프레임리스 + 네이티브 상단바, MDL2 창제어 글리프
- 2c 지연 WebView2 프리뷰 패널
- 2d Tab 들여쓰기(리스트 인식) + 리스트 자동 이어쓰기 + 표 동작(Tab/Shift+Tab 셀 이동, 마지막 열 Tab 열 추가, Enter 행 추가, Ctrl+Shift+F 정렬)
- 2e 표 삽입 대화상자(Ctrl+T, 열/행 입력), 설정 직렬화(saveSettings), 설정 대화상자(Ctrl+,, defaultView/theme/fontSize/tabSize/wrap/scrollSync/highlightLanguages 런타임 적용)
- 스크롤 동기화(에디터 -> 프리뷰, 분할 + scrollSync)
- 정리: 구 web UI를 `web/legacy/`로 이동, README/FEATURES 갱신

앱 상태: 메모장급 즉시 로딩, 분할/미리보기에서 라이브 프리뷰(마크다운/KaTeX/Prism), Tab 들여쓰기/리스트 이어쓰기/표 편집, 표 삽입 및 설정 대화상자, 스크롤 동기화. 파일 새로/열기/저장, 보기 전환(Ctrl+1/2/3), 디바이더 드래그, 닫기 확인(MessageBox).

## 남은 작업

- 단축키 재바인딩(keymap): 현재 단축키는 ACCEL 테이블 하드코딩이다. `settings.keymap`을 읽어 동적 ACCEL을 구성하고, 설정 대화상자에 키 입력 캡처 UI를 추가하는 작업이 남았다(별도 작업으로 분리).
- main 병합 전 수동 점검: 콜드/웜 기동 재측정, 한글 IME 입력, 대용량 파일, 표/설정/스크롤 동기화 상호작용 확인.

### 결정 완료

- 리스트 들여쓰기 폭: `tabSize`(기본 4칸) 유지.
- 구 web UI: 삭제 대신 `web/legacy/`로 이동해 참고용 보존.

## 핵심 기술 메모 (재개 시 참고)

- 아키텍처: `src/main.cpp`가 네이티브 창을 소유(webview.h의 창 소유를 대체). 좌측 EDIT(`g_edit`), 우측 STATIC(`g_preview`)에 WebView2 지연 임베드. 보기 배치는 `layout()` / `setView()`.
- 지연 생성: `setView`에서 보기가 에디터 전용(0)이 아닐 때만 `ensureWebview()`. 에디터 전용은 WebView를 아예 안 만든다.
- 네이티브 -> 프리뷰: `g_webview->eval("window.mymdRender(\"<base64>\")")` 등. 함수 `pushPreviewNow` / `pushPreviewConfig` / `refreshPreview`. preview.js API: `mymdRender` / `mymdSetTheme` / `mymdSetBase` / `mymdSetLangs` / `mymdScrollTo`. 준비 핸드셰이크: `mymdPreviewReady`(bind).
- 프리뷰 -> 네이티브: 외부 링크 열기 `mymdOpenExternal`만 bind.
- webview.h 패치: 외부 창 임베드 리사이즈용 공개 메서드 `update_bounds()` 추가(`resize_widget` 래핑). 패널 이동/리사이즈 후 호출.
- EDIT 서브클래스: `EditProc`(WM_KEYDOWN/WM_CHAR). 원본은 `g_editProc`. Tab/Enter 처리 후 `g_swallowChar`로 뒤따르는 WM_CHAR를 소거.
- 렌더 디바운스: EN_CHANGE -> `SetTimer(IDT_RENDER, 120)` -> `pushPreviewNow`.
- 리스트 파싱/들여쓰기: `parseListItem` / `orderedStartNum`(형제 인식 번호) / `splitLines` / `blockIndent` / `doTabIndent` / `doListEnter`.
- 설정: `loadSettings`가 fontSize/tabSize/wrap/theme/defaultView/highlightLanguages(원문 배열)를 읽는다. 쓰기는 미구현.
