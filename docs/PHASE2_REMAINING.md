# Phase 2 (에디터 네이티브 분리) 남은 작업

브랜치: `feature/editor-separation`. 전체 설계는 `proto/PHASE1_FINDINGS.md`와 승인된 계획 참고.

## 현재 상태 (완료)

- 2a 네이티브 EDIT 에디터 셸 (`5867d71`)
- 2b 프레임리스 + 네이티브 상단바 (`a32531d`), MDL2 창제어 글리프 (`1e390b8`)
- 2c 지연 WebView2 프리뷰 패널 (`873660e`)
- 2d 일부: Tab 들여쓰기(리스트 인식) + 리스트 자동 이어쓰기 (`5e8c794`, `555f3d6`)

앱 상태: 메모장급 즉시 로딩(에디터 ~100ms 웜), 분할/미리보기에서 라이브 프리뷰(마크다운/KaTeX/Prism), Tab 들여쓰기와 리스트 이어쓰기 동작. 파일 새로/열기/저장, 보기 전환(미리/분할/편집, Ctrl+1/2/3), 디바이더 드래그, 닫기 확인(MessageBox).

## 남은 작업

### 2d 표 동작 (web/app.js 로직 이식)

`src/main.cpp`의 `EditProc`에서 `doTabIndent`/`doListEnter`보다 먼저 표 컨텍스트를 검사하도록 추가한다.

- 표 안 Tab/Shift+Tab 셀 이동: app.js `tableNav`
- 표 행에서 Enter 시 자동 새 행 추가: app.js `tableEnter`
- 표 정렬(셀 폭 맞춤 재정렬): app.js `formatTableAtCaret` (Ctrl+Shift+F)
- 표 삽입(스켈레톤 생성): app.js `insertTableSkeleton` (2e 대화상자와 연결, Ctrl+T)

### 2e 네이티브 대화상자

- 표 삽입 대화상자(열/행 입력) -> `insertTableSkeleton` 이식. 상단바 "표" 버튼 + Ctrl+T.
- 설정 대화상자: defaultView / theme / fontSize / tabSize / wrap / scrollSync / 단축키(keymap) / highlightLanguages.
  - settings.json 쓰기(직렬화)가 필요하다. 현재는 읽기 전용 최소 파서(`jsonStr`/`jsonInt`/`jsonBool`/`jsonArrayRaw`)만 있다.
  - 적용 방식: 폰트/탭폭은 즉시(applyEditStyle), wrap은 EDIT 스타일이 생성 시 고정이라 컨트롤 재생성 필요, 테마는 `g_editBrush` 재생성 + 리페인트 + 프리뷰 `mymdSetTheme` 통지, highlightLanguages는 `mymdSetLangs` 통지.
  - 단축키 캡처 UI. 상단바 "설정" 버튼 + Ctrl+,.

### 기타 보류 항목

- 스크롤 동기화(에디터 -> 프리뷰): `preview.js`에 `window.mymdScrollTo(ratio)` 이미 있음. 네이티브에서 EDIT 스크롤 감지(`EditProc`의 WM_VSCROLL / WM_MOUSEWHEEL / 캐럿 이동) -> 비율 계산 -> `g_webview->eval("window.mymdScrollTo(r)")`. `settings.scrollSync`로 토글.
- 상단바에 표/정렬/설정 버튼 추가(2d/2e와 함께). 현재 상단바는 파일(새 파일/열기/저장) + 보기 세그먼트 + 창 제어만.
- 키맵: 현재 단축키는 하드코딩(Ctrl+N/O/S/Shift+S/1/2/3). `settings.keymap` 반영은 2e에서.
- 들여쓰기 폭 결정: 현재 리스트 들여쓰기는 `tabSize`(설정값, 기본 4칸). 사용자 예시는 2칸이었음. 2칸 고정 vs tabSize 결정 필요(ordered 마커 폭이 3이라 2칸은 CommonMark 중첩이 애매할 수 있음).
- 런타임 테마 변경: 현재는 시작 시 settings/시스템 테마만 반영. 설정에서 바꾸면 브러시 재생성 + 리페인트 + 프리뷰 통지 필요.
- wrap 런타임 토글: EDIT 스타일은 생성 시 고정. 변경하려면 컨트롤 재생성.
- 정리: `web/index.html`, `web/app.js`(구 all-in-WebView UI)는 이 브랜치에서 미사용. 전환 완료 후 제거하거나 참고용으로 보존(`preview.js`의 모태). `README.md`/`docs/FEATURES.md` 갱신 필요.
- main 병합 전 점검: 콜드/웜 기동 재측정, 한글 IME 입력, 대용량 파일.

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
