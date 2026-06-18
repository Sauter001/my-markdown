# MyMD

간이 마크다운 에디터. Windows 네이티브(Win32 + WebView2) 애플리케이션으로, 메모장처럼 즉시 로딩되어 편집할 수 있고 좌측 편집/우측 렌더링을 병치한다.

## 특징
- 미니멀 UI, 즉시 로딩(단일 native exe + 로컬 자산)
- 마크다운 기본 문법, 표, 취소선, 작업 목록
- LaTeX 수식(KaTeX, 오프라인 폰트 포함), `<details>` 토글, 이미지 렌더링
- 미리보기 코드 블록 구문 강조(Prism, 라이트/다크 테마 연동, 강조 언어 설정 가능)
- 미리보기의 외부 링크는 기본 브라우저로 열림(앱 내부 탐색 방지)
- 보기 모드: 에디터만 / 병치 / 미리보기만 (단축키로 전환, 기본 보기 설정 가능)
- 파일을 열지 않아도 편집 가능, Ctrl+S로 특정 .md 파일에 저장
- 단축키와 기본 보기 등은 설정에서 변경(`settings.json`에 영속화)

## 사용법
- 실행: `run.bat` 또는 `MyMD.exe` 더블클릭. 인자로 `.md` 경로를 주면 그 파일을 열고 시작한다.
  - 예: `run.bat samples\welcome.md`
- 상단바: 파일명 표시, 새 파일 / 열기 / 저장 버튼, 보기 모드 전환(⟮ ⟯ / ⟮⟯ / ⟯ ⟮), 설정(⚙)

### 기본 단축키 (설정에서 변경 가능)
| 동작 | 단축키 |
| --- | --- |
| 저장 | Ctrl+S |
| 다른 이름으로 저장 | Ctrl+Shift+S |
| 열기 | Ctrl+O |
| 새 파일 | Ctrl+N |
| 에디터만 보기 | Ctrl+1 |
| 병치 보기 | Ctrl+2 |
| 미리보기만 보기 | Ctrl+3 |
| 보기 순환 | Ctrl+\ |
| 설정 열기 | Ctrl+, |

## 빌드
사전 준비:
- 리포지토리 포함: `third_party\webview`(webview 0.10.0 헤더), `third_party\webview2\build\native\include`(WebView2 SDK 헤더), `web\vendor`(markdown-it, KaTeX 및 폰트, Prism).
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
  src/main.cpp        네이티브 호스트 (창, 파일 대화상자/입출력, 제목, 설정 영속화, JS 브리지)
  web/index.html      UI 골격
  web/app.css         스타일 (라이트/다크)
  web/app.js          렌더링, 보기 모드, 단축키, 설정 로직
  web/vendor/         markdown-it, KaTeX(+폰트), Prism(코드 강조)
  third_party/        mingw64, webview, webview2 SDK
  samples/welcome.md  데모 문서
  build.bat / run.bat
```

### 동작 개요
- UI 전체는 WebView2 안의 로컬 HTML/JS이며, C++는 네이티브 기능만 담당한다.
- JS와 C++ 브리지(`webview.bind`)는 인코딩 문제를 피하려고 모든 동적 문자열(내용, 경로, 파일명, 설정)을 base64로 주고받는다.
- 파일 저장은 네이티브 입출력이라 Ctrl+S가 같은 파일에 확실히 기록된다(브라우저 샌드박스 제약 없음).

## 알려진 한계 / 향후 작업
- 미리보기 코드 블록은 Prism으로 강조하지만, 입력 영역(에디터)은 단순 textarea라 강조가 없다(즉시 로딩 우선). 필요 시 CodeMirror 등으로 확장 가능.
- 상대 경로 이미지는 현재 문서 폴더 기준 `file://`로 해석한다. 일부 경로에서 막히면 C++에서 data URI로 읽어오는 방식으로 보강 가능.
- 인쇄/찾기 등은 WebView2 기본 동작에 의존한다.
- MSVC 빌드 스크립트는 미포함(요청 시 추가).
