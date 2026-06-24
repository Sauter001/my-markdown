# web/legacy (참고용 보존)

`feature/editor-separation` 브랜치에서 에디터를 네이티브 컨트롤로 분리하면서 더 이상 로드되지 않는 구 all-in-WebView UI다.

- `index.html`, `app.js`: 예전에는 UI 전체(에디터 + 미리보기 + 상단바)를 WebView2 안에서 그렸다. 현재 앱은 `MyMD.exe`(네이티브 창 + EDIT 컨트롤)가 UI를 소유하고, 우측 패널에만 WebView2를 지연 임베드해 `preview.html` / `preview.js`로 미리보기를 렌더링한다.
- 보존 이유: `preview.js`의 마크다운/KaTeX/Prism 렌더링과 표/리스트 편집 로직(`src/main.cpp`로 이식됨)의 원본 참고.
- 주의: `index.html`은 `app.css`를 상대경로로 참조하지만 `app.css`는 `web/`에 남아 있다(`preview.html`과 공유). 이 폴더의 파일은 실행에 쓰이지 않으므로 깨진 상대경로는 무시해도 된다.
