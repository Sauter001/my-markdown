# MyMD

<img src="mymd-logo.png" width="200" />

간이 마크다운 에디터. Windows 네이티브(Win32) 애플리케이션으로, 좌측 편집은 RichEdit 컨트롤(평문 모드), 우측 렌더링은 지연 임베드한 WebView2 미리보기로 처리한다. 메모장처럼 즉시 로딩되어 편집할
수 있다.

## 특징

- 미니멀 UI, 즉시 로딩(단일 native exe + 로컬 자산)
- 다단계 실행 취소/다시 실행(Ctrl+Z / Ctrl+Y), 전체 선택(Ctrl+A), 라인 잘라내기/복사/삭제(선택 없을 때 Ctrl+X / Ctrl+C / Ctrl+Del)
- 괄호/따옴표/백틱/별표 자동 페어링(백틱은 코드 펜스로 확장, 설정에서 끌 수 있음), Enter 시 들여쓰기 유지(코드 블록 중괄호는 한 단계 증가)
- 마크다운 기본 문법, 표, 취소선, 작업 목록
- LaTeX 수식(KaTeX, 오프라인 폰트 포함), `<details>` 토글, 이미지 렌더링
- 미리보기 코드 블록 구문 강조(Prism, 라이트/다크 테마 연동, 강조 언어 설정 가능)
- 미리보기의 외부 링크는 기본 브라우저로 열림(앱 내부 탐색 방지)
- 현재 파일을 VSCode로 열기(상단바 버튼 / Ctrl+Shift+V, 저장 후 실행)
- 보기 모드: 에디터만 / 병치 / 미리보기만 (단축키로 전환, 기본 보기 설정 가능)
- 파일을 열지 않아도 편집 가능, Ctrl+S로 특정 .md 파일에 저장
- 단축키와 기본 보기 등은 설정에서 변경(`settings.json`에 영속화)

## 사용법

- 실행: `run.bat` 또는 `MyMD.exe` 더블클릭. 인자로 `.md` 경로를 주면 그 파일을 열고 시작한다.
    - 예: `run.bat samples\welcome.md`
- 상단바: 파일명 표시, 새 파일 / 열기 / 저장 / VSCode / 표 / 정렬 / 설정 버튼, 보기 모드 전환(편집 / 분할 / 미리보기)

### 기본 단축키 (설정에서 변경 가능)

| 동작               | 단축키                      |
|------------------|--------------------------|
| 저장               | Ctrl+S                   |
| 다른 이름으로 저장       | Ctrl+Shift+S             |
| 열기               | Ctrl+O                   |
| 새 파일             | Ctrl+N                   |
| VSCode로 열기       | Ctrl+Shift+V             |
| 에디터만 보기          | Ctrl+1                   |
| 병치 보기            | Ctrl+2                   |
| 미리보기만 보기         | Ctrl+3                   |
| 보기 순환            | Ctrl+\                   |
| 표 삽입             | Ctrl+T                   |
| 표 정렬             | Ctrl+Shift+F             |
| 설정 열기            | Ctrl+,                   |
| 확대 / 축소 / 배율 초기화 | Ctrl++ / Ctrl+- / Ctrl+0 |

위 표는 재바인딩 가능한 동작이다. 그 밖에 에디터 고정 편집 키가 있다(재바인딩 대상 아님): 전체 선택(Ctrl+A), 실행 취소/다시 실행(Ctrl+Z / Ctrl+Y), 잘라내기/복사/붙여넣기(Ctrl+X / Ctrl+C / Ctrl+V), 라인 잘라내기/복사(선택이 없을 때 Ctrl+X / Ctrl+C), 라인 삭제(Ctrl+Del).

## 빌드

사전 준비:

- 리포지토리 포함: `third_party\webview`(webview 0.10.0 헤더), `third_party\webview2\build\native\include`(WebView2 SDK 헤더),
  `web\vendor`(markdown-it, KaTeX 및 폰트, Prism).
- 별도 준비(용량이 커서 미포함): `third_party\mingw64`(WinLibs MinGW-w64 g++). 아래 중 하나로 받아 `third_party\mingw64`에 위치시킨다.
    - winget: `winget install --id BrechtSanders.WinLibs.POSIX.UCRT`
    - 또는 WinLibs zip 압축 해제: https://github.com/brechtsanders/winlibs_mingw/releases

```
build.bat
```

정적 링크라 결과 `MyMD.exe` 단독으로 실행된다(런타임 DLL 불필요). 실행에는 시스템에 WebView2 런타임이 설치되어 있어야 한다(Windows 10/11에는 기본 포함).

## 구조

```
MyMD/
  src/main.cpp        진입점 (App 생성 + run)
  src/App.*           조립/메인 윈도우 프로시저/콜백 배선
  src/core/           인코딩, JSON, 파일 입출력, 마크다운, DPI, RAII (순수 로직)
  src/model/          Settings, Theme, Keymap
  src/ui/             Editor, TableEditor, Topbar, Preview, dialogs/*
  web/preview.html    미리보기 골격 (우측 패널 WebView2)
  web/preview.js      마크다운/KaTeX/Prism 렌더링, 미리보기 API
  web/app.css         스타일 (라이트/다크)
  web/legacy/         구 all-in-WebView UI (index.html, app.js) - 미사용, 참고용 보존
  web/vendor/         markdown-it, KaTeX(+폰트), Prism(코드 강조)
  third_party/        mingw64, webview, webview2 SDK
  samples/welcome.md  데모 문서
  build.bat / run.bat
```

### 동작 개요

- 네이티브 호스트가 UI를 소유한다. 좌측은 RichEdit 컨트롤(평문 모드, 에디터), 우측 패널에만 WebView2 미리보기를 지연 임베드한다(에디터 전용 보기에서는 미리보기를 만들지 않아 즉시 로딩).
- 호스트 -> 미리보기는 `eval`로 `window.mymd*`(렌더/테마/base/언어/키맵/줌/스크롤)를 호출하고, 동적 문자열은 base64로 전달한다. 미리보기 -> 호스트는 준비 통지, 외부
  링크 열기, 단축키 통지를 `bind` 한다.
- 파일 저장은 네이티브 입출력이라 Ctrl+S가 같은 파일에 확실히 기록된다.

## 알려진 한계 / 향후 작업

- 미리보기 코드 블록은 Prism으로 강조하지만, 입력 영역(에디터)은 RichEdit 평문 모드라 강조가 없다(즉시 로딩 우선).
- 상대 경로 이미지는 현재 문서 폴더 기준 `file://`로 해석한다. 일부 경로에서 막히면 C++에서 data URI로 읽어오는 방식으로 보강 가능.
- 인쇄/찾기 등은 WebView2 기본 동작에 의존한다.
- MSVC 빌드 스크립트는 미포함(요청 시 추가).
