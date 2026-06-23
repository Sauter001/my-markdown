# 첫 렌더(프리뷰) 콜드 스타트 최적화 기록

분리된 네이티브 에디터 + WebView2 프리뷰 구조에서 "첫 프리뷰가 유달리 느리다"는
증상을 추적하고 최적화한 과정을 정리한다. 관련 코드: `src/main.cpp`,
`web/preview.html`, `web/preview.js`. 요약 결정은 `PHASE2_REMAINING.md` 참고.

## 1. 증상과 초기 가설

- 증상: 에디터는 메모장급으로 즉시 뜨지만, 프리뷰의 첫 렌더 결과가 나오기까지가
  체감상 약 0.5초로 느렸다.
- 초기 가설(스레딩): "렌더링과 에디터가 같은 스레드라 느린 것 아닌가."
- 확인 결과: 아니다. WebView2 는 멀티프로세스 엔진이라 마크다운 파싱/하이라이트/
  수식 렌더는 별도 렌더러 프로세스에서 돈다. 호스트는 `ICoreWebView2::ExecuteScript`
  (webview.h 의 `eval`)로 비동기 디스패치만 한다(즉시 리턴). 즉 C++ 측에 워커
  스레드를 더해도 의미 없다. 병목은 "스레드 수"가 아니라 다른 곳에 있었다.

## 2. 측정 방법

추측을 멈추고 계측했다. 임시 계측 코드(진단 후 제거)로 다음 구간의 경과 시간을
`QueryPerformanceCounter` 기준으로 로그에 찍었다.

- C++ 측 마커: 프로세스 진입(t0), 에디터 페인트 완료(`editor_painted`),
  엔진 생성 시작/완료(`engine_create_start`/`engine_ready`), 프리뷰 준비
  (`preview_ready`).
- JS 측 마커: `render()` 내부에서 `md.render` / `innerHTML` / `highlight` 각 단계
  소요와 총합을 측정해 호스트 bind 로 회신.
- 대상 문서: 약 12KB, 194줄, 코드블록 2개(1개는 빈 언어), 표 41행, 수식/이미지 없음.

브라우저 인자 실험은 `MYMD_WV2ARGS` 환경변수를 받아 WebView2 가 읽는
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` 로 전달하도록 해, 재빌드 없이 인자를
바꿔가며 비교했다. OS 캐시 편차를 줄이려 워밍업 1회 후 시나리오별 3회 측정.

## 3. 측정 결과

### 3.1 콜드 스타트 분해 (3회 평균, 프로세스 시작 기준)

| 구간                                     | 시간   | 성격                                                 |
| ---------------------------------------- | ------ | ---------------------------------------------------- |
| 프로세스 시작 -> 에디터 페인트           | ~120ms | 앱/창/파일 셋업                                      |
| 엔진 콜드 부팅 (`new webview`)           | ~480ms | Chromium 프로세스 spawn + COM 핸드셰이크 (고유 비용) |
| navigate + 라이브러리 + 렌더러 첫 페인트 | ~180ms | app.css + markdown-it + preview.js 로드, 첫 페인트   |
| 실제 마크다운 렌더                       | ~20ms  | md.render ~22 / innerHTML ~1 / highlight ~0.5        |
| 합계 (시작 -> 첫 프리뷰)                 | ~800ms |                                                      |

핵심: **실제 마크다운 렌더는 약 20ms 로 빠르다.** 0.5초의 정체는 마크다운 처리가
아니라 WebView2 엔진을 세션에서 처음 한 번 띄우는 비용이다.

### 3.2 브라우저 인자 실험 (효과 없음)

엔진 부팅 시간(`engine_create_start` -> `engine_ready`):

| 시도 | Baseline | 인자 적용 |
| ---- | -------- | --------- |
| #1   | 470ms    | 497ms     |
| #2   | 505ms    | 506ms     |
| #3   | 466ms    | 438ms     |
| 평균 | ~480ms   | ~481ms    |

적용 인자: `--disable-gpu --disable-background-networking
--disable-features=msSmartScreenProtection,msWebOOUI,msPdfOOUI --no-pings
--disable-sync --disable-extensions --disable-component-update`.

결론: 인자로는 줄지 않는다. 런 간 편차(438-506ms)가 인자 효과보다 크다. 480ms 는
Chromium 임베드의 바닥값으로, 코드 레벨 인자 튜닝으로는 못 깎는다. 실패한 실험이라
관련 코드는 제거했다.

## 4. 적용한 최적화

측정에 근거해 "콜드 부팅을 가리는 가림막(스피너 등)"이 아니라 실제로 경로를
줄이거나 옮기는 변경만 반영했다.

1. **WebView2 엔진 백그라운드 프리웜** (`src/main.cpp`)
   - `new webview` 는 동기 블로킹(준비될 때까지 중첩 메시지 루프). 이걸 시작
     경로(`setView`)에서 빼고, 에디터 첫 페인트 뒤 `PostMessage(WM_APP_PREWARM)`
     -> `ensureWebview()` 로 미룬다.
   - 기본 보기와 무관하게(에디터 전용 포함) 미리 생성해 둔다. 이후 프리뷰 진입은
     즉시. `setView` 의 `ensureWebview()` 호출은 멱등이라 중복 생성하지 않는다.
   - 한계: 엔진 생성 자체는 여전히 UI 스레드 동기 블로킹이다. WebView2 컨트롤러는
     COM STA 스레드 친화성이 있어 워커 스레드로 빼면 이후 `eval`/`update_bounds`
     호출이 깨진다. 그래서 "병렬"이 아니라 "에디터 첫 페인트 뒤로 미뤄 체감에서
     숨김"이다.

2. **markdown-it 정적 로드** (`web/preview.html`)
   - 기존에는 첫 렌더 시점에 `injectScript` 로 지연 주입했다. preview.html 은
     에디터와 별개 문서라 여기서 지연 로드해도 에디터 로딩에 이득이 없고, 첫 렌더에
     직렬 비용만 쌓였다.
   - `<script src="vendor/markdown-it.min.js">` 정적 로드로 바꿔 navigation 과
     겹치게 하고, 첫 렌더의 inject 후 재렌더 왕복을 제거했다.

3. **파서(md) 사전 빌드** (`web/preview.js`)
   - preview.js 로드 즉시 `ensureMarkdownIt()` 를 호출해 파서를 미리 만든다.
     markdown-it 가 정적 로드되어 즉시 빌드 가능하다. 첫 텍스트가 도착하면 lazy
     라운드트립 없이 바로 렌더.

4. **Prism 유휴 프리페치** (`web/preview.js`)
   - 첫 렌더 후 `requestIdleCallback`(없으면 `setTimeout` 폴백)으로 Prism 을 미리
     받아 다음 코드블록 편집을 즉시화한다.
   - KaTeX(수식)는 수식 없는 문서가 약 1.5MB(JS + 폰트)를 끌지 않도록 on-demand
     유지. 첫 수식 등장 시에만 로드.

## 5. 현재 결론

- 렌더링 파이프라인 자체는 이미 빠르다(웜 렌더 약 20ms).
- 남은 체감 지연은 WebView2 콜드 부팅(약 480ms)이고, 이는 Chromium 고유 비용으로
  인자 튜닝으로는 줄지 않음을 측정으로 확인했다.
- 이 비용은 **세션당 1회**다. 프리웜으로 엔진을 데워두니, 앱을 켜놓고 문서를 여러
  개 열면 두 번째부터는 즉시다. 매번 앱을 새로 켜서 첫 문서를 보는 패턴일 때만
  약 800ms 를 만난다.
- 현 상태(프리웜 + 정적 로드 + 사전 빌드 + 유휴 프리페치)를 합리적 최적점으로
  수용했다.

## 6. 남은 레버 (미착수, 필요 시)

첫 프리뷰의 절대 시간을 더 줄여야 할 때의 후보. 효과 순으로 모두 modest 하고,
480ms 바닥은 못 넘는다.

1. **비동기 엔진 초기화**: 엔진 부팅을 앱 셋업(약 120ms)과 겹치도록 webview.h 의
   동기 embed 를 비동기로 개조. 현실적 절감 약 100-150ms. 중간 난이도(컨트롤러
   수명/navigate 순서 주의).
2. **navigate 단계 축소**: 더 작은 파서로 교체하거나 자산 인라인. 약 30-60ms.
3. **렌더러 교체**: Chromium 을 안 띄우는 방향. 480ms 바닥을 넘는 유일한 길이지만
   아키텍처 대공사다.

## 부록: 측정 재현

- 임시 계측 코드는 진단 후 제거했다. 재현하려면 `render()` 와 엔진 생성/준비 지점에
  `QueryPerformanceCounter` 마커를 다시 심고, 파일 인자로 실행하면(`MyMD.exe <md>`)
  시작 시 자동 로드 -> 프리웜 -> `onPreviewReady` -> 첫 렌더 경로가 자동으로 돈다.
- 브라우저 인자 비교는 `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` 환경변수를 엔진
  생성 전에 설정하면 webview.h 수정 없이 적용된다.
