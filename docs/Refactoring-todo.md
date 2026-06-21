# MyMD 리팩토링 TODO

원래 단일 `src/main.cpp`(약 1,950줄)와 `web/preview.js`를 훑고 정리한 리팩토링 후보 목록.
기능 변경이 아니라 구조 개선이 목적이며, 각 항목은 현황(파일/심볼), 문제, 제안 순으로 적는다.

- 작성: 2026-06-20 / 갱신: 2026-06-21
- 대상: `feature/editor-separation` 브랜치 기준
- 분류: (A) OOP/캡슐화, (B) 디자인 패턴 도입, (C) 일반 리팩토링/중복 제거, (D) RichEdit 전환 이후 신규
- 우선순위: P1(구조의 뿌리, 효과 큼) / P2(중복/가독성) / P3(소소한 정리)
- 진행: A1/A2(앱 객체 + 모듈 분리), C6(디스패치 통합)은 완료, A3/C1은 부분 완료. 이후 에디터가 RichEdit로 전환되며 신규 후보(D 섹션)가 생겼다. `main.cpp`는 현재 진입점만(약
  13줄) 남았다.

> 실무 제약: `build.bat`는 이제 `-Isrc` 와 여러 `.cpp`(약 16개)를 함께 컴파일한다(A2 분리 반영). 소스를 추가/이동하면 `build.bat`의 소스 목록도 함께 갱신해야 한다.

---

## 요약 (우선순위 순)

| 순위 | 항목                                 | 분류  | 핵심                                                    |
|----|------------------------------------|-----|-------------------------------------------------------|
| P1 | 전역 상태를 `App` 객체로 캡슐화 (완료)          | A   | 30여 개 `g_*` 전역 -> 인스턴스 멤버                             |
| P1 | 액션을 Command 테이블로 통합                | B   | 한 액션이 4곳에 흩어진 중복 제거                                   |
| P1 | 단일 파일을 모듈로 분리 (완료)                 | A/C | 책임별 .h/.cpp + 협력 클래스 분할                               |
| P1 | `Settings` 모델 도입 (부분 완료)           | A/C | 모델/load/save/clamp 완료, `highlightLanguages`만 원문 배열 유지 |
| P2 | 보기 모드 enum + 매핑 단일화 (부분)           | B   | `commands.h` `VIEW_*` 상수만, `view_` int/문자열 매핑은 미정리    |
| P2 | 모달 대화상자 공통 헬퍼 (부분)                 | B   | 클래스 + `dlg_util.h`로 분리, 모달 루프는 대화상자별 중복 잔존            |
| P2 | GDI/핸들 RAII 래퍼 (A3 완료, C2 일부)      | A/C | 누수 방지, 코드 축소                                          |
| P2 | `runBtn`/`WM_COMMAND` 디스패치 통합 (완료) | C   | WM_COMMAND가 모든 명령을 `runBtn`으로 위임                      |
| P2 | 표 편집을 `Table` 값 타입으로               | B/C | 버퍼 직접 조작 -> 파싱/포맷/직렬화 분리                              |
| P2 | `Theme` 캐싱 객체                      | A   | `isDark()`가 호출마다 레지스트리 읽기(미캐싱)                        |
| P1 | 액션 Command 테이블 (B1)                | B   | 버튼/액셀/디스패치 단일 정의(미착수, 최대 효과)                          |
| P2 | (D) 죽은 `WM_CTLCOLOREDIT` 정리        | C/D | RichEdit 전환으로 미호출, 색은 메시지로 적용                         |
| P2 | (D) 단일 줄바꿈(LF) 불변식 중앙화             | C/D | `\n` 삽입/정규화가 Editor/TableEditor에 분산                   |
| P2 | (D) 에디터 입력 디스패치 정리                 | B/D | `Editor::onMessage` 키 처리 비대                           |
| P3 | 매직 넘버/좌표 상수화                       | C   | 컨트롤 ID, 레이아웃, 타이밍                                     |
| P3 | 언어 목록 C++/JS 동기화 정리                | C   | 3중 중복(`kAllLangs`/`langsJson`/`LANGUAGES`)            |
| P3 | 메시지/에러 로컬라이즈 일관성                   | C   | 영어 "write failed" 등                                   |

---

## (A) OOP/캡슐화

### A1. 전역 상태를 `App` 객체로 모은다 (P1) - 완료(2026-06-20)

- 현황: 파일 최상단에 `g_curPath`, `g_dirty`, `g_hwnd`, `g_edit`, `g_view`, `g_webview`, `g_btns`, `g_setDlgOk` 등 30개가 넘는
  `static` 전역이 흩어져 있고, 거의 모든 함수가 이들을 직접 읽고 쓴다.
- 문제: 생명주기와 소유 관계가 코드에 드러나지 않는다. 어떤 함수가 무엇을 바꾸는지 추적하기 어렵고, 테스트나 재진입이 불가능하며, 초기화 순서 의존(`refreshPreview` 전방 선언 등)이 숨어 있다.
- 제안: 상태를 묶는 `App`(또는 `Editor`/`MainWindow`) 클래스를 두고 `g_*`를 멤버로 옮긴다. `WNDPROC`는 정적 thunk로 받아
  `GetWindowLongPtr(GWLP_USERDATA)`에 저장한 인스턴스로 위임한다(전형적인 Win32 OOP 래핑). 단일 인스턴스라도 상태 그룹화와 생명주기 명시 효과가 크다.
- 적용 결과: 모든 `g_*` 전역을 `App` 클래스 멤버(`m_*`)로 이동했다. 상태를 다루는 함수는 멤버 메서드가 되고, 인코딩/JSON/리스트/표 포맷 같은 순수 로직은 자유 함수로 남겼다(A2 모듈 분리
  시 그대로 떼어낼 수 있는 형태). 메인 창/EDIT 서브클래스/표/설정 대화상자 프로시저는 정적 thunk가 `GWLP_USERDATA`(또는 EDIT USERDATA)에 저장한 인스턴스로 위임한다.
  `refreshPreview` 전방 선언이 사라졌고(멤버 간 호출은 순서 무관), `main()`은 `App app; return app.run();` 로 축소됐다. `WM_NCCREATE` 이전 메시지(예:
  최초 `WM_GETMINMAXINFO`)는 인스턴스 미설정이라 기본 처리되지만, 창 생성 크기가 최소 크기보다 커서 관측 가능한 차이는 없다.

### A2. 책임별 모듈 분리 (P1) - 완료(2026-06-20)

- 현황: 인코딩 변환, base64, 파일 입출력, JSON 파서, 설정, 테마, 상단바, 에디터 입력 동작, 표 편집(약 480줄), 프리뷰 브리지, 스크롤 동기화, 대화상자 2종, 메인 윈도우 프로시저가 모두
  한 파일에 있다.
- 문제: 한 파일이 거의 2,000줄이라 탐색/리뷰/충돌 비용이 크고, 경계가 주석(`// ---`)으로만 구분된다.
- 제안: 다음 단위로 헤더/구현 분리를 검토한다.
    - `str_util`(utf8<->wide, base64, file URL), `file_io`(read/write/경로 도우미)
    - `settings`(모델 + JSON), `theme`(색/폰트/다크 판정)
    - `topbar`(레이아웃/페인트/히트테스트), `editor`(리스트/Tab/들여쓰기)
    - `table_edit`(표), `preview`(WebView 브리지 + 스크롤 동기화)
    - `dialogs`(모달 헬퍼 + 표/설정), `app`(윈도우 프로시저 + 진입점)
- 비고: 순수 로직(JSON, 리스트 파싱, 표 포맷)은 Win32 의존이 없어 분리 시 단위 테스트가 가능해진다.
- 적용 결과: 단일 `main.cpp`를 책임별 .h/.cpp + 협력 클래스로 분해했다. `main.cpp`는 `int main { App app; return app.run(); }` 만 남는다.
    - `src/core/`: `raii.h`(GdiHandle/UniqueHandle), `str_util`, `json`, `file_io`, `markdown`(리스트/표 순수 파싱). Win32 컨트롤
      비의존이라 단위 테스트 가능.
    - `src/model/`: `Settings`(load/save/clampRange), `Theme`(다크 판정 + 색).
    - `src/ui/`: `Editor`(EDIT 소유 + Tab/Enter/들여쓰기), `TableEditor`(EDIT 버퍼 표 조작), `Topbar`(레이아웃/페인트/히트), `Preview`(
      WebView2; `webview.h`를 `Preview.cpp` 한 TU에만 가둠), `dialogs/TableDialog`, `dialogs/SettingsDialog`. 공용 헬퍼
      `ui/edit_util.h`, `ui/dialogs/dlg_util.h`.
    - `src/App`(조립 + 메인 윈도우 프로시저 + 콜백 배선), `src/commands.h`(명령 ID).
    - 컴포넌트 간 통지는 `std::function` 콜백으로 단방향 배선(Editor.onTableNav/onTableEnter/onScroll, Preview.onReady/onOpenExternal).
      설정 대화상자는 값만 편집해 반환하고 런타임 적용은 App이 전후 스냅샷 비교로 수행(원본의 dialog 내 인라인 적용을 App으로 이동).
    - `build.bat`를 멀티 소스로 갱신(`-Isrc` + 14개 .cpp). 빌드/경고 점검 통과(새 경고 없음).

### A3. 리소스 소유를 타입으로 표현 (P2, A9와 연결) - 완료(2026-06-20)

- 현황: `g_webview = new webview::webview(...)`는 해제되지 않고, GDI 폰트/브러시/펜과 `HANDLE`은 수동으로 Create/Delete 짝을 맞춘다.
- 제안: 소유를 `std::unique_ptr`/RAII 래퍼로 표현한다(자세한 내용은 C 섹션 RAII 항목).
- 적용 결과: `GdiHandle<T>`(HFONT/HBRUSH/HPEN)와 `UniqueHandle`(커널 HANDLE) RAII 래퍼를 추가했다. 글꼴/브러시는 `App` 멤버(`FontHandle`/
  `BrushHandle`)로 소유해 소멸 시 자동 해제하고, `readFile`/`writeFile`/`launchVSCode`의 파일/프로세스 핸들과 `paintTopbar`/디바이더의 임시 브러시/펜을 스코프
  RAII로 정리했다. `webview`는 `std::unique_ptr` 멤버가 되어 `WM_DESTROY`에서 호스트 창이 살아 있을 때 정리한다(기존 누수 제거). 남은 RAII 정리는 C2 항목 참고.

---

## (B) 디자인 패턴 도입 후보

### B1. Command 테이블 (P1, 가장 효과 큼)

- 현황: 액션 하나(예: 표 삽입)를 추가하려면 네 곳을 함께 손봐야 한다.
    1. `layoutTopbar`의 `items[]` 버튼 정의
    2. `runBtn`의 switch
    3. `MainProc`의 `WM_COMMAND` switch (일부는 `runBtn`에 위임, 일부는 중복 인라인)
    4. `WinMain`의 `ACCEL accels[]` 단축키 테이블
- 문제: 추가/변경 시 누락 위험이 크고, `WM_COMMAND`와 `runBtn`이 같은 ID를 이중 처리한다.
- 제안: `{ id, 라벨, 단축키, 핸들러(std::function), 표시 위치 }` 한 줄로 액션을 정의하는 Command 레지스트리를 만든다. 상단바 버튼/액셀러레이터 테이블/디스패치를 이 한 곳에서
  생성하도록 한다(Command 패턴). 액션 추가가 1곳으로 줄고 `runBtn` switch가 사라진다.

### B2. 보기 모드 enum + 단일 매핑 (P2)

- 현황: `g_view`는 `int`이고 `0=에디터, 1=분할, 2=미리보기` 의미가 `layout()`, `setView()`, `paintTopbar()`(`bv` 계산), `WinMain`(문자열 ->
  정수), 설정 대화상자(`kViews[]` + `comboIndex`)에 흩어져 있다.
- 문제: 매직 정수가 반복되고 문자열<->정수 매핑이 두 곳에 중복된다.
- 제안: `enum class View { Editor, Split, Preview }` 도입, 문자열<->enum 변환을 한 함수로 모은다. 보기별 레이아웃 분기는 향후 State 패턴으로도 확장 가능하지만,
  우선 enum화만으로 충분하다.
- 현황 갱신(부분): `commands.h`에 `VIEW_EDITOR/SPLIT/PREVIEW` 상수를 도입해 매직 정수 의미는 명시됐다. 다만 `App::view_`는 여전히 `int`이고, 문자열<->정수
  매핑이 `App::run`(defaultView 파싱)과 `SettingsDialog`(`kViews`) 두 곳에 남아 있다. `enum class` 전환 + 변환 단일화는 미적용.

### B3. 모달 대화상자 공통 헬퍼 (P2)

- 현황: `showTableDialog`와 `showSettingsDialog`가 거의 동일한 뼈대를 복제한다: 윈도우 클래스 1회 등록(`static bool reg`), 부모 중앙 팝업 생성, 컨트롤 생성
  `mk` 람다, `EnableWindow(g_hwnd,FALSE)`, Enter/Esc 처리 모달 루프, 정리/포커스 복귀. 프로시저도 `IDOK/IDCANCEL/WM_CLOSE` 패턴이 같고
  `g_tblDlg*` / `g_setDlg*` 전역 플래그 쌍이 따로 있다.
- 제안: `ModalDialog` 헬퍼(또는 `runModal(부모, 크기, 제목, populate, onOk)`)로 공통 루프/생성/정리를 캡슐화한다. `mk`(컨트롤 빌더)도 멤버로 흡수한다. 전역 플래그 쌍이
  제거되고 새 대화상자 추가 비용이 준다.
- 현황 갱신(부분): 대화상자를 `TableDialog`/`SettingsDialog`(+`ShortcutDialog`) 클래스로 분리하고 컨트롤 빌더/숫자 읽기 등을 `ui/dialogs/dlg_util.h`로
  공유했다. 다만 모달 메시지 루프(Enter/Esc + `EnableWindow` + 정리)는 각 `show()`에 여전히 중복돼 `runModal` 류 공통화 여지가 남는다.

### B4. 표를 값 타입으로 모델링 (P2, C 섹션 중복과 연결)

- 현황: `doFormatTable`/`tableAddColumn`/`doTableEnter`/`insertTableSkeleton`이 EDIT 버퍼 좌표를 직접 다루며, "셀 -> 행 문자열", "행들 ->
  `\r\n` 결합", "블록 범위 계산 후 replace" 패턴을 각자 반복한다.
- 제안: `struct Table { rows, cols, align }` 값 타입과 `Table::parse(text)`, `format()`, `toText()`를 두고, EDIT 연동(범위 추출/치환/셀
  선택)은 얇은 어댑터로 분리한다. 포맷 로직이 순수 함수가 되어 테스트 가능해지고 중복이 사라진다.

### B5. 액션/설정 적용의 Observer 경량화 (P3)

- 현황: 설정 저장 후 `showSettingsDialog` 말미에서 글꼴/탭/테마/wrap/언어 변경을 일일이 비교해 `applyEditStyle`/`recreateEditForWrap`/브러시 재생성/
  `refreshPreview`를 호출한다.
- 제안: 큰 패턴까지는 과하다. `Settings`에 "무엇이 바뀌었는가"를 반환하는 작은 diff를 두고 적용 루틴을 한 함수로 모으는 정도면 충분하다.

---

## (C) 일반 리팩토링/중복 제거

### C1. `Settings` 모델로 일원화 (P1)

- 현황: `loadSettings`/`saveSettings`와 `jsonStr/jsonInt/jsonBool/jsonArrayRaw/jsonValuePos`가 손수 만든 최소 JSON 파서다.
  `highlightLanguages`는 `std::vector<std::string>`이 아니라 원시 JSON 배열 문자열 `g_langsJson`으로 보관해, `parseLangsJson`/
  `csvToLangsJson` 왕복 변환이 필요하다. 클램프(`fontSize` 10-32, `tabSize` 1-8)는 `loadSettings`와 설정 대화상자 두 곳에 중복된다.
- 제안: 타입이 있는 `Settings` 구조체(언어는 `vector<string>`)와 `load()`/`save()`/`clampInRange()`를 한 곳에 둔다. 기본값/검증/클램프를 단일화하고, 언어는
  메모리에서 벡터로 다루며 직렬화 시에만 JSON으로 만든다. 가능하면 검증된 소형 JSON 헤더(헤더온리) 채택도 검토.
- 적용 결과(부분 완료): `model/Settings`에 타입 있는 구조체 + `load`/`save`/`clampRange` 도입. fontSize/tabSize/zoom 클램프를 모델로 일원화했고
  `autoPair`/`keymap` 등 항목을 추가했다. 남은 부분: `highlightLanguages`는 여전히 원문 JSON 문자열(`langsJson`)로 보관해 `SettingsDialog`의
  `parseLangsJson`/`csvToLangsJson` 왕복이 남아 있다(벡터화 미적용).

### C2. RAII 리소스 래퍼 (P2)

- 현황: `paintTopbar`는 매 호출마다 `CreateSolidBrush`/`CreatePen` 후 `DeleteObject`를 반복한다. `readFile`/`writeFile`는 모든 반환 경로에서
  `CloseHandle`을 직접 호출한다. `launchVSCode`는 `PROCESS_INFORMATION` 핸들을 수동 닫는다. `g_webview`는 해제되지 않는다.
- 문제: 경로가 늘면 누수/이중 해제 위험이 커지고 코드가 장황하다.
- 제안: `GdiObject`(HFONT/HBRUSH/HPEN), `UniqueHandle`(HANDLE), 스코프 DC, `select` 가드 같은 작은 RAII 유틸을 둔다. 자주 쓰는 테마 브러시/펜은
  캐싱한다(아래 C5와 연결).

### C3. 줄/텍스트 처리 중복 (P2)

- 현황: 줄 분리기가 둘이다. `splitLines`(에디터용)와 `tblGetLines`(표용, 오프셋 포함). EDIT 텍스트 취득도 `getEditText`(UTF-8/LF)와 `editGetTextW`(
  wide/CRLF)로 나뉘어 둘 다 `GetWindowTextLengthW`+`GetWindowTextW`를 한다. "행들 -> `\r\n` 결합"은 표 코드 여러 곳과 들여쓰기 코드에 반복된다.
- 제안: wide 텍스트 취득 코어 하나(`getEditTextW()`)로 모으고 변환은 그 위에 얹는다. `joinCrlf(const vector<wstring>&)` 헬퍼를 추가해 결합 중복을 없앤다. 가능하면
  줄 분리도 오프셋 옵션을 가진 하나로 통합한다.
- 현황 갱신(부분): 텍스트 취득은 `ui/edit_util.h::editGetTextW`로 모았고 RichEdit 전환에 맞춰 LF로 정규화한다(`getTextUtf8Lf`는 그 위에 얹음). 줄 분리기는 여전히
  `splitLines`(에디터)와 `tblGetLines`(표, 오프셋 포함)로 둘이고, "행들 -> 줄바꿈 결합"도 곳곳에 반복된다. 결합 헬퍼는 이제 `\n` 기준(D2 참고)이며 줄 분리 통합은 미적용.

### C4. 매직 넘버/좌표 상수화 (P3)

- 현황: 컨트롤 ID가 원시 정수로 흩어져 있다(표 대화상자 `1001/1002`, 설정 대화상자 `2001`-`2006`은 리터럴인데 `2007`-`2010`만 최근 enum화됨). 대화상자/상단바 레이아웃
  좌표가 하드코딩이고, `TopBtn::type`의 `0/1/2/3` 의미는 주석에만 있다. 디바운스 값(`IDT_RENDER` 120ms, 스크롤 16ms)도 리터럴이며 문서에는 "약 110ms"로 적혀 코드(
  120)와 어긋난다.
- 제안: 컨트롤 ID는 enum으로 모으고(설정 대화상자 ID 전체 포함), `TopBtn::type`도 `enum class BtnType { Text, Caption, Close, ViewSeg }`로 바꾼다.
  타이밍 상수에 이름을 주고 문서 수치와 맞춘다.

### C5. 테마 해석/색 캐싱 (P2)

- 현황: `isDarkTheme()`는 테마가 "system"일 때 호출마다 레지스트리(`AppsUseLightTheme`)를 읽고, `themeBg/Fg/Topbar/Border/...`가 페인트 1회에 여러 번
  이를 호출한다.
- 제안: 해석된 다크/라이트 상태와 색/브러시를 `Theme` 객체에 캐싱하고, 테마 변경 또는 `WM_SETTINGCHANGE` 때만 갱신한다(시스템 테마 추종 시 반응성도 좋아진다).
- 현황 갱신(미적용): `model/Theme` 객체는 생겼으나 `isDark()`가 여전히 호출마다 레지스트리(`AppsUseLightTheme`)를 읽고 `bg()/fg()/...`가 이를 반복 호출한다(캐싱
  없음). RichEdit 전환으로 에디터 색도 `Editor::applyColors`에서 테마색을 받으므로 캐싱 + 변경/`WM_SETTINGCHANGE` 시 갱신이 여전히 유효하다.

### C6. `runBtn`과 `WM_COMMAND` 디스패치 통합 (P2, B1로 흡수 가능)

- 현황: `WM_COMMAND`가 `IDM_NEW/OPEN/SAVE/SAVEAS/VSCODE`는 인라인 처리하고 표/설정은 `runBtn`에 위임하는 등 경로가 갈린다.
- 제안: 모든 명령 ID를 `runBtn`(혹은 B1의 Command 디스패치)으로 단일 경로화한다.
- 적용 결과(완료): `WM_COMMAND`가 모든 명령 ID를 `runBtn`으로 위임한다(인라인 중복 제거). 다만 상단바 버튼/액셀러레이터/디스패치를 한 정의에서 생성하는 Command 테이블(B1)은 여전히
  미착수로, 가장 큰 구조 개선 여지로 남는다.

### C7. 언어 목록 3중 중복 동기화 (P3)

- 현황: 지원 언어 식별자가 세 곳에 있다. C++ `kAllLangs`(설정 UI 후보), C++ `g_langsJson` 기본값, JS `web/preview.js`의 `LANGUAGES` 테이블 +
  `g_langs` 기본값. Prism 별칭/컴포넌트는 JS에만 있다.
- 문제: 언어 추가 시 C++/JS 양쪽을 손으로 맞춰야 하고 어긋나기 쉽다.
- 제안: 단기적으로는 두 파일 상단에 "동기화 필요" 교차 주석을 명시(이미 `kAllLangs`에 일부 있음)하고, 중기적으로는 단일 원본(예: 작은 JSON/생성 스크립트)에서 양쪽을 만들어내는 방식을
  검토한다.

### C8. 메시지/에러 로컬라이즈 일관성 (P3)

- 현황: UI는 한국어인데 입출력 실패 메시지는 영어다(`"write failed"`, `"read failed"`, `"load fail: ..."`). 확인 메시지는 한글이 UTF-8 바이트 이스케이프(
  `\xEC\xA0\x80...`)로 박혀 있어 읽기 어렵다.
- 제안: 사용자 노출 문자열을 한 곳(`W(u8"...")` 또는 문자열 테이블)에 모으고 언어를 일관되게 맞춘다. 바이트 이스케이프 리터럴은 `u8"..."`로 정리한다.

### C9. `main()` 분해 (P3)

- 현황: `main()`이 인자 파싱, 설정 로드, 폰트/브러시 생성, 클래스 등록, 창 생성, 프레임리스 적용, 인자 파일 로드, 표시, 액셀러레이터 구성, 메시지 루프를 모두 한다. 여기서 만든 GDI 자원은
  해제되지 않는다(프로세스 종료로 회수되지만 소유가 불명확).
- 제안: `initResources()`, `createMainWindow()`, `buildAccelTable()`, `runMessageLoop()`로 쪼개고, 자원 소유를 A1의 `App`에 귀속시킨다.

---

## (D) RichEdit 전환 이후 신규 후보

### D1. 죽은 `WM_CTLCOLOREDIT` 정리 (P2)

- 현황: 에디터를 RichEdit로 바꾼 뒤에도 `App::onMessage`에 `WM_CTLCOLOREDIT` 핸들러가 남아 있다. RichEdit는 이 메시지를 보내지 않으므로 호출되지 않는 죽은 코드다.
  에디터 색은 `Editor::applyColors`(`EM_SETBKGNDCOLOR`/`EM_SETCHARFORMAT`)로 적용한다.
- 제안: 핸들러를 제거한다. `editBrush_`는 메인 창 클래스 배경(`wc.hbrBackground`)에만 쓰이므로 그 용도로 한정하거나 `windowBrush_`로 이름을 명확히 한다.

### D2. 단일 줄바꿈(LF) 불변식 중앙화 (P2, C3와 연결)

- 현황: RichEdit는 줄바꿈을 내부 단일 CR(1문자)로 센다. 위치(EM_SETSEL 등) 정합을 위해 `editGetTextW`가 LF로 정규화하고, 그 결과 "삽입 문자열의 줄바꿈은 단일 `\n`"이라는
  불변식이 생겼다. 이를 `Editor`(blockIndent/listEnter/autoIndentEnter/autoPair 펜스)와 `TableEditor`(골격/정렬/행 추가) 여러 곳이 손으로 지킨다.
- 문제: 누군가 `"\r\n"`을 삽입하면 RichEdit가 1문자로 정규화해 길이 기반 캐럿 계산이 어긋난다(회귀 위험). 표 삽입 코드는 실제로 줄바꿈 길이로 캐럿 오프셋을 계산한다.
- 제안: `joinLf(const vector<wstring>&)` 헬퍼와 "캐럿 오프셋은 LF 길이로 계산"이라는 규칙을 헤더 주석으로 명문화하고, 길이로 캐럿을 잡는 경로는 헬퍼를 거치게 한다.

### D3. 에디터 입력 디스패치 정리 (P2)

- 현황: `Editor::onMessage`의 `WM_KEYDOWN`이 Tab/Enter/Backspace + Ctrl 단축(A/C/X/Y/Del) + 네비게이션을 인라인 if 사슬로 처리한다. 자동 페어링/라인
  편집/들여쓰기 추가로 분기가 커졌다.
- 제안: `{ 조건(modifier+vk), 핸들러 }` 작은 테이블이나 `handleKey()` 분리로 정리한다. Enter 처리의 우선순위(표 -> 리스트 -> 들여쓰기)도 한 곳에서 읽히게 한다.

### D4. 코드 블록 판별 공유 (P3)

- 현황: `Editor::inCodeBlock`/`isFenceLine`(펜스 줄 개수 휴리스틱)은 별표 페어링/들여쓰기 분기에만 쓰인다. 미리보기(`preview.js`)는 markdown-it가 별도로
  코드블록을 판정한다.
- 제안: 당장은 단일화 불필요. 에디터에 문맥 의존 기능이 더 늘면 한 곳으로 모으는 것을 검토한다.

## 권장 적용 순서 (남은 작업, 위험 낮은 것부터)

A1/A2(앱 객체 + 모듈 분리), C6(디스패치 통합)은 완료. 남은 후보 기준 순서:

1. D1(죽은 `WM_CTLCOLOREDIT` 정리), C4(상수/enum화) - 동작 영향 거의 없음, 즉시 정리.
2. D2(LF 불변식 헬퍼) + C3(텍스트/결합 중복), C5(Theme 캐싱) - 회귀 위험 축소 + 성능.
3. B2(View enum 마무리), C1(언어 벡터화) - 데이터 모델 정리.
4. D3(에디터 입력 디스패치 정리), B3(모달 루프 공통화) - 비대한 분기/중복 정리.
5. B1(Command 테이블) - 버튼/액셀/디스패치 단일 정의(최대 효과, 가장 큰 변경).
6. B4(`Table` 값 타입) - 표 포맷 순수화/테스트 가능화.

각 단계는 독립 커밋으로 분리하고, 빌드(`build.bat`)와 수동 점검(콜드 기동, 한글 IME, RichEdit 전환 회귀: 멀티라인 위치 정합/더티/테마색)을 사이에 끼운다.
