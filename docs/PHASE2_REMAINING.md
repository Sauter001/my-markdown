# Phase 2 (에디터 네이티브 분리) 남은 작업

브랜치: `feature/editor-separation`. 전체 설계는 `proto/PHASE1_FINDINGS.md`와 승인된 계획 참고.

## 현재 상태 (완료)

(커밋 해시는 리베이스로 바뀔 수 있어 적지 않음. `git log --oneline`의 `feat(editor-separation)` 참고.)

- 2a 네이티브 에디터 셸 (초기 EDIT 컨트롤, 이후 RichEdit 평문 모드로 전환 - 아래 별도 항목)
- 2b 프레임리스 + 네이티브 상단바, MDL2 창제어 글리프
- 2c 지연 WebView2 프리뷰 패널
- 2d Tab 들여쓰기(리스트 인식) + 리스트 자동 이어쓰기 + 표 동작(Tab/Shift+Tab 셀 이동, 마지막 열 Tab 열 추가, Enter 행 추가, Ctrl+Shift+F 정렬)
- 2e 표 삽입 대화상자(Ctrl+T, 열/행 입력), 설정 직렬화(saveSettings), 설정 대화상자(Ctrl+,,
  defaultView/theme/fontSize/tabSize/wrap/scrollSync/highlightLanguages 런타임 적용)
- 스크롤 동기화(에디터 -> 프리뷰, 분할 + scrollSync)
- 글자 배율(zoom): Ctrl +/- 로 에디터/프리뷰 공통 배율 조절, 설정에 직렬화(`mymdSetZoom`)
- 첫 렌더 콜드 스타트 최적화: 시작 직후 WebView2 엔진 백그라운드 프리웜(`WM_APP_PREWARM`, 에디터 전용 기본값도 미리 생성), `markdown-it` 정적 로드(preview.html),
  preview.js 로드 시 파서 사전 빌드, 첫 렌더 후 Prism 유휴 프리페치(`requestIdleCallback`)
- 정리: 구 web UI를 `web/legacy/`로 이동, README/FEATURES 갱신
- 단축키 재바인딩(keymap): 동적 ACCEL(`model/Keymap.h::buildAccels`) + 설정 대화상자 키 캡처(`ui/dialogs/ShortcutDialog`), `settings.keymap` 오버라이드, 미리보기 포커스 경로(`preview.js` keydown -> `mymdAccel`)까지 동기화 (이전 "남은 작업"에서 완료)
- OOP/모듈 리팩토링: 단일 `main.cpp`(약 1,950줄)를 `App`(조립) + `core`/`model`/`ui` 모듈로 분리하고 `g_*` 전역 제거(자세한 내역/남은 후보는 `docs/Refactoring-todo.md`)
- 에디터 컨트롤 RichEdit(평문 모드) 전환: 다단계 실행 취소/다시 실행(Ctrl+Z/Ctrl+Y), 자동 페어링(괄호/따옴표/백틱/별표), 라인 편집(Ctrl+X/C/Del), Enter 들여쓰기 유지(코드 블록 중괄호 증가), 테마색은 `EM_SETBKGNDCOLOR`/`EM_SETCHARFORMAT`로 적용. 위치 계산은 LF로 통일(RichEdit 내부 단일 CR 줄바꿈과 정합)

앱 상태: 메모장급 즉시 로딩, 분할/미리보기에서 라이브 프리뷰(마크다운/KaTeX/Prism), Tab 들여쓰기/리스트 이어쓰기/표 편집, 표 삽입 및 설정 대화상자, 스크롤 동기화, 글자 배율. 파일
새로/열기/저장, 보기 전환(Ctrl+1/2/3), 디바이더 드래그, 닫기 확인(MessageBox). 프리뷰 엔진은 시작 직후 백그라운드 프리웜으로 웜 상태 유지(첫 프리뷰만 WebView2 콜드 부팅 비용,
세션당 1회).

## 남은 작업

- main 병합 전 수동 점검: 한글 IME 입력, 대용량 파일, 표/설정/스크롤 동기화 상호작용, 그리고 RichEdit 전환 후 회귀(멀티라인 들여쓰기/리스트/표/자동 페어링의 캐럿 위치 정합, EN_CHANGE 더티/렌더,
  테마색, 스크롤 동기화 비율) 확인. (콜드/웜 기동은 측정 완료, 아래 결정 참고.)
- 구조 개선 후보는 `docs/Refactoring-todo.md` 참고(Command 테이블 B1, View enum 정리 B2, Theme 색 캐싱 C5, RichEdit 전환으로 죽은 `WM_CTLCOLOREDIT` 정리 등).

### 결정 완료

- 리스트 들여쓰기 폭: `tabSize`(기본 4칸) 유지.
- 구 web UI: 삭제 대신 `web/legacy/`로 이동해 참고용 보존.
- 첫 렌더 콜드 스타트: 시작 -> 첫 프리뷰 약 800ms 측정(에디터 페인트 ~120ms + 엔진 부팅 ~480ms + navigate/라이브러리 ~180ms + 실제 렌더 ~20ms, 3회 평균). 엔진 부팅
  480ms 는 WebView2(Chromium) 프로세스 spawn 고유 비용으로, 브라우저 인자(`--disable-gpu`, SmartScreen/백그라운드 네트워킹 차단 등)로 줄지 않음을 측정으로 확인.
  세션당 1회 비용이며 프리웜으로 데워두면 이후 렌더는 약 20ms(웜). 현 상태(프리웜 + 정적 로드 + 사전 빌드 + 유휴 프리페치)를 합리적 최적점으로 수용. 추가 단축이 필요하면 후보는 (1) 비동기 엔진
  초기화로 엔진 부팅을 앱 셋업과 겹치기(약 100-150ms 절감, webview.h 동기 embed 개조 필요, 미착수), (2) navigate 단계 축소(더 작은 파서/인라인, ~30-60ms), (3)
  렌더러 교체(아키텍처 변경, 대공사).

## 핵심 기술 메모 (재개 시 참고)

> 갱신(2026-06-21): 아래 메모는 리팩토링 전 단일 `main.cpp`(`g_*` 전역, `EditProc`/`doTabIndent`/`loadSettings` 등) 시점 기준이라 심볼명이 현재 코드와 다르다. 현재는 모듈 구조(`App` 조립 + `core`/`model`/`ui`, `Editor`/`Preview`/`TableEditor` 클래스)이고 에디터는 RichEdit(평문 모드)다. 텍스트 취득은 `ui/edit_util.h::editGetTextW`가 LF 정규화해 반환하고, 위치 계산은 LF 기준이다. 최신 구조/동작은 `README.md`, `docs/FEATURES.md`, `docs/Refactoring-todo.md` 참고. 아래는 설계 의도의 역사적 기록으로 남긴다.

- 아키텍처: `src/main.cpp`가 네이티브 창을 소유(webview.h의 창 소유를 대체). 좌측 EDIT(`g_edit`), 우측 STATIC(`g_preview`)에 WebView2 지연 임베드. 보기
  배치는 `layout()` / `setView()`.
- 엔진 생성/프리웜: 시작 직후 첫 페인트 뒤 `PostMessage(WM_APP_PREWARM)` -> `ensureWebview()`로 기본 보기와 무관하게(에디터 전용 포함) 엔진을 백그라운드로 미리 만든다.
  `setView`의 `ensureWebview()`(보기가 0이 아닐 때) 호출은 멱등이라 중복 생성하지 않는다. `new webview`는 동기 블로킹(중첩 메시지 루프)이라 첫 페인트 뒤로 미뤄 에디터를 먼저
  띄운다.
- preview.js 로딩: `markdown-it`는 preview.html에서 정적 로드, preview.js 로드 즉시 `ensureMarkdownIt()`로 파서 사전 빌드. KaTeX/Prism은 지연
  로드(KaTeX는 수식이 있을 때만), 첫 렌더 후 `schedulePrefetch()`가 `requestIdleCallback`으로 Prism을 미리 받는다.
- 네이티브 -> 프리뷰: `g_webview->eval("window.mymdRender(\"<base64>\")")` 등. 함수 `pushPreviewNow` / `pushPreviewConfig` /
  `pushPreviewZoom` / `refreshPreview`. preview.js API: `mymdRender` / `mymdSetTheme` / `mymdSetBase` / `mymdSetLangs` /
  `mymdScrollTo` / `mymdSetZoom`. 준비 핸드셰이크: `mymdPreviewReady`(bind).
- 프리뷰 -> 네이티브: 외부 링크 열기 `mymdOpenExternal`만 bind.
- webview.h 패치: 외부 창 임베드 리사이즈용 공개 메서드 `update_bounds()` 추가(`resize_widget` 래핑). 패널 이동/리사이즈 후 호출.
- EDIT 서브클래스: `EditProc`(WM_KEYDOWN/WM_CHAR). 원본은 `g_editProc`. Tab/Enter 처리 후 `g_swallowChar`로 뒤따르는 WM_CHAR를 소거.
- 렌더 디바운스: EN_CHANGE -> `SetTimer(IDT_RENDER, 120)` -> `pushPreviewNow`.
- 리스트 파싱/들여쓰기: `parseListItem` / `orderedStartNum`(형제 인식 번호) / `splitLines` / `blockIndent` / `doTabIndent` /
  `doListEnter`.
- 설정: `loadSettings`/`saveSettings`가 fontSize/tabSize/wrap/theme/defaultView/scrollSync/highlightLanguages(원문 배열)/zoom 을
  읽고 쓴다. 설정 대화상자(Ctrl+,)와 줌 변경에서 `saveSettings()` 호출.
