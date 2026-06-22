/* MyMD - 프리뷰 전용 렌더러 (네이티브 에디터와 분리된 WebView2 패널)
 *
 * 네이티브 호스트가 마크다운 텍스트/테마/이미지 base/언어 목록을 보내면 렌더한다.
 * markdown-it 는 preview.html 에서 정적 로드(첫 렌더 콜드 스타트 단축), KaTeX/Prism 는 지연 로드.
 * 호스트로의 콜백은 외부 링크 열기(mymdOpenExternal)와 준비 신호(mymdPreviewReady)뿐.
 */
(function () {
  'use strict';

  // base64 (UTF-8 안전)
  function b64e(str) {
    const bytes = new TextEncoder().encode(str);
    let bin = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    return btoa(bin);
  }
  function b64d(b64) {
    if (!b64) return '';
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new TextDecoder().decode(bytes);
  }

  const preview = document.getElementById('preview');

  // 호스트가 채워주는 상태
  let g_baseUrl = '';
  let g_langs = ['bash', 'c', 'cpp', 'java', 'python', 'html', 'css', 'javascript', 'sql', 'json'];
  let g_keymap = [];  // [{id,ctrl,shift,alt,key}] - 미리보기 포커스 단축키(호스트 동기화)
  let lastText = '';

  // ---------------------------------------------------------------------------
  // markdown-it + KaTeX (지연 로드)
  // ---------------------------------------------------------------------------
  let md = null, mdLoading = null, katexLoading = null, mathPending = false;

  // 헤더 anchor slug. C++ src/core/markdown.cpp 의 slugify 와 규칙이 반드시
  // 동일해야 한다(같은 raw 입력 -> 같은 결과). 목차 링크 target 과 일치 보장.
  //   A-Z->소문자, a-z 0-9 _ 유지, 비ASCII(>=0x80) 유지, 공백류->'-',
  //   그 외 ASCII 문장부호 삭제, 연속 '-' 합치고 앞뒤 '-' 제거.
  function slugify(raw) {
    let o = '';
    for (const ch of raw) {
      const c = ch.codePointAt(0);
      if (c >= 0x41 && c <= 0x5A) o += ch.toLowerCase();
      else if ((c >= 0x61 && c <= 0x7A) || (c >= 0x30 && c <= 0x39) || c === 0x5F) o += ch;
      else if (c >= 0x80) o += ch;
      else if (ch === ' ' || ch === '\t') o += '-';
    }
    let r = '';
    for (const ch of o) { if (ch === '-' && r.endsWith('-')) continue; r += ch; }
    return r.replace(/^-+|-+$/g, '');
  }
  // heading_open 토큰에 slug id 부여(중복은 -1,-2 ...). inline.content(헤더 raw
  // 원문)를 쓰므로 C++ parseHeadings 와 동일 입력 -> 동일 slug.
  function addHeadingIds(state) {
    const seen = {};
    const toks = state.tokens;
    for (let i = 0; i < toks.length; i++) {
      if (toks[i].type !== 'heading_open') continue;
      const inline = toks[i + 1];
      let base = slugify(inline && inline.type === 'inline' ? inline.content : '');
      if (!base) base = 'section';
      const n = seen[base] || 0; seen[base] = n + 1;
      toks[i].attrSet('id', n === 0 ? base : base + '-' + n);
    }
  }

  function buildMd() {
    const m = window.markdownit({ html: true, linkify: true, typographer: true, breaks: false });
    applyKatex(m);
    m.core.ruler.push('mymd_heading_ids', addHeadingIds);
    m.validateLink = function () { return true; };
    return m;
  }
  function ensureMarkdownIt() {
    if (md) return Promise.resolve(md);
    if (!mdLoading) {
      mdLoading = (window.markdownit ? Promise.resolve() : injectScript('vendor/markdown-it.min.js'))
        .then(() => { md = buildMd(); return md; }).catch(() => null);
    }
    return mdLoading;
  }
  function loadStylesheetOnce(href) {
    if (document.querySelector('link[data-mymd-css="' + href + '"]')) return;
    const l = document.createElement('link');
    l.rel = 'stylesheet'; l.href = href; l.setAttribute('data-mymd-css', href);
    document.head.appendChild(l);
  }
  function ensureKatex() {
    if (window.katex) return Promise.resolve(window.katex);
    if (!katexLoading) {
      loadStylesheetOnce('vendor/katex/katex.min.css');
      katexLoading = injectScript('vendor/katex/katex.min.js').then(() => window.katex || null).catch(() => null);
    }
    return katexLoading;
  }

  function applyKatex(md) {
    function isValidDelim(state, pos) {
      const max = state.posMax;
      let canOpen = true, canClose = true;
      const prev = pos > 0 ? state.src.charCodeAt(pos - 1) : -1;
      const next = pos + 1 <= max ? state.src.charCodeAt(pos + 1) : -1;
      if (prev === 0x20 || prev === 0x09 || (next >= 0x30 && next <= 0x39)) canClose = false;
      if (next === 0x20 || next === 0x09) canOpen = false;
      return { canOpen, canClose };
    }
    function mathInline(state, silent) {
      if (state.src[state.pos] !== '$') return false;
      let res = isValidDelim(state, state.pos);
      if (!res.canOpen) { if (!silent) state.pending += '$'; state.pos += 1; return true; }
      const start = state.pos + 1;
      let match = start, pos;
      while ((match = state.src.indexOf('$', match)) !== -1) {
        pos = match - 1;
        while (state.src[pos] === '\\') pos -= 1;
        if ((match - pos) % 2 === 1) break;
        match += 1;
      }
      if (match === -1) { if (!silent) state.pending += '$'; state.pos = start; return true; }
      if (match - start === 0) { if (!silent) state.pending += '$$'; state.pos = start + 1; return true; }
      res = isValidDelim(state, match);
      if (!res.canClose) { if (!silent) state.pending += '$'; state.pos = start; return true; }
      if (!silent) {
        const token = state.push('math_inline', 'math', 0);
        token.markup = '$'; token.content = state.src.slice(start, match);
      }
      state.pos = match + 1;
      return true;
    }
    function mathBlock(state, start, end, silent) {
      let firstLine, lastLine, next, lastPos, found = false;
      let pos = state.bMarks[start] + state.tShift[start];
      let max = state.eMarks[start];
      if (pos + 2 > max) return false;
      if (state.src.slice(pos, pos + 2) !== '$$') return false;
      pos += 2;
      firstLine = state.src.slice(pos, max);
      if (silent) return true;
      if (firstLine.trim().slice(-2) === '$$') { firstLine = firstLine.trim().slice(0, -2); found = true; }
      for (next = start; !found;) {
        next++;
        if (next >= end) break;
        pos = state.bMarks[next] + state.tShift[next];
        max = state.eMarks[next];
        if (pos < max && state.tShift[next] < state.blkIndent) break;
        if (state.src.slice(pos, max).trim().slice(-2) === '$$') {
          lastPos = state.src.slice(0, max).lastIndexOf('$$');
          lastLine = state.src.slice(pos, lastPos); found = true;
        }
      }
      state.line = next + 1;
      const token = state.push('math_block', 'math', 0);
      token.block = true;
      token.content = (firstLine && firstLine.trim() ? firstLine + '\n' : '')
        + state.getLines(start + 1, next, state.tShift[start], true)
        + (lastLine && lastLine.trim() ? lastLine : '');
      token.map = [start, state.line]; token.markup = '$$';
      return true;
    }
    const renderInline = (latex) => {
      if (!window.katex) { mathPending = true; return '<span class="katex-pending">' + escapeHtml('$' + latex + '$') + '</span>'; }
      try { return window.katex.renderToString(latex, { displayMode: false, throwOnError: false }); }
      catch (e) { return escapeHtml(latex); }
    };
    const renderBlock = (latex) => {
      if (!window.katex) { mathPending = true; return '<p class="katex-block katex-pending">' + escapeHtml('$$' + latex + '$$') + '</p>'; }
      try { return "<p class='katex-block'>" + window.katex.renderToString(latex, { displayMode: true, throwOnError: false }) + '</p>'; }
      catch (e) { return '<p class="katex-block">' + escapeHtml(latex) + '</p>'; }
    };
    md.inline.ruler.after('escape', 'math_inline', mathInline);
    md.block.ruler.after('blockquote', 'math_block', mathBlock, { alt: ['paragraph', 'reference', 'blockquote', 'list'] });
    md.renderer.rules.math_inline = (tokens, idx) => renderInline(tokens[idx].content);
    md.renderer.rules.math_block = (tokens, idx) => renderBlock(tokens[idx].content) + '\n';
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }
  function taskListPostProcess(html) {
    return html
      .replace(/<li>\s*\[ \]\s*/g, '<li class="task-list-item"><input class="task-list-item-checkbox" type="checkbox" disabled> ')
      .replace(/<li>\s*\[[xX]\]\s*/g, '<li class="task-list-item"><input class="task-list-item-checkbox" type="checkbox" checked disabled> ');
  }

  // ---------------------------------------------------------------------------
  // 코드 구문 강조 (Prism, 지연 로드)
  // ---------------------------------------------------------------------------
  const LANGUAGES = [
    { id: 'bash',       prism: 'bash',       aliases: ['bash', 'sh', 'shell', 'zsh'],     comps: ['bash'] },
    { id: 'c',          prism: 'c',          aliases: ['c', 'h'],                          comps: ['clike', 'c'] },
    { id: 'cpp',        prism: 'cpp',        aliases: ['cpp', 'c++', 'cc', 'cxx', 'hpp'],  comps: ['clike', 'c', 'cpp'] },
    { id: 'java',       prism: 'java',       aliases: ['java'],                            comps: ['clike', 'java'] },
    { id: 'python',     prism: 'python',     aliases: ['python', 'py'],                    comps: ['python'] },
    { id: 'html',       prism: 'markup',     aliases: ['html', 'markup', 'xml', 'svg'],    comps: ['markup'] },
    { id: 'css',        prism: 'css',        aliases: ['css'],                             comps: ['css'] },
    { id: 'javascript', prism: 'javascript', aliases: ['javascript', 'js', 'mjs', 'jsx'], comps: ['clike', 'javascript'] },
    { id: 'sql',        prism: 'sql',        aliases: ['sql'],                             comps: ['sql'] },
    { id: 'json',       prism: 'json',       aliases: ['json', 'jsonc'],                   comps: ['json'] },
    { id: 'typescript', prism: 'typescript', aliases: ['typescript', 'ts'],                comps: ['clike', 'javascript', 'typescript'] },
    { id: 'yaml',       prism: 'yaml',       aliases: ['yaml', 'yml'],                     comps: ['yaml'] },
    { id: 'go',         prism: 'go',         aliases: ['go', 'golang'],                    comps: ['go'] },
    { id: 'rust',       prism: 'rust',       aliases: ['rust', 'rs'],                      comps: ['rust'] },
    { id: 'csharp',     prism: 'csharp',     aliases: ['csharp', 'cs', 'c#', 'dotnet'],    comps: ['clike', 'csharp'] },
    { id: 'kotlin',     prism: 'kotlin',     aliases: ['kotlin', 'kt', 'kts'],             comps: ['clike', 'kotlin'] },
    { id: 'markdown',   prism: 'markdown',   aliases: ['markdown', 'md'],                  comps: ['markup', 'markdown'] },
    { id: 'diff',       prism: 'diff',       aliases: ['diff', 'patch'],                   comps: ['diff'] },
    { id: 'ini',        prism: 'ini',        aliases: ['ini', 'cfg', 'conf'],              comps: ['ini'] },
    { id: 'toml',       prism: 'toml',       aliases: ['toml'],                            comps: ['toml'] },
  ];
  const LANG_BY_ID = {};
  LANGUAGES.forEach((l) => { LANG_BY_ID[l.id] = l; });

  function enabledAliasMap() {
    const map = {};
    for (const id of g_langs) {
      const lang = LANG_BY_ID[id];
      if (!lang) continue;
      for (const a of lang.aliases) map[a.toLowerCase()] = lang.prism;
    }
    return map;
  }
  function neededComponents() {
    const seen = new Set(); const out = [];
    for (const id of g_langs) {
      const lang = LANG_BY_ID[id];
      if (!lang) continue;
      for (const c of lang.comps) if (!seen.has(c)) { seen.add(c); out.push(c); }
    }
    return out;
  }
  const prismLoaded = new Set();
  let prismChain = Promise.resolve();
  function injectScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src; s.async = false;
      s.onload = () => resolve();
      s.onerror = () => reject(new Error('load fail: ' + src));
      document.head.appendChild(s);
    });
  }
  function ensurePrism() {
    const comps = neededComponents();
    prismChain = prismChain.then(async () => {
      window.Prism = window.Prism || {};
      window.Prism.manual = true;
      if (!prismLoaded.has('core')) { await injectScript('vendor/prism/prism-core.min.js'); prismLoaded.add('core'); }
      for (const c of comps) { if (prismLoaded.has(c)) continue; await injectScript('vendor/prism/components/prism-' + c + '.min.js'); prismLoaded.add(c); }
    }).catch(() => {});
    return prismChain;
  }
  function highlightCode() {
    if (!g_langs.length) return;
    const aliasMap = enabledAliasMap();
    const blocks = preview.querySelectorAll('pre > code[class*="language-"]');
    if (!blocks.length) return;
    const P = window.Prism;
    let any = false, needLoad = false;
    blocks.forEach((code) => {
      const m = /(?:^|\s)language-(\S+)/.exec(code.className);
      if (!m) return;
      const prismName = aliasMap[m[1].toLowerCase()];
      if (!prismName) return;
      if (m[1] !== prismName) code.className = (code.className.replace(/(?:^|\s)language-\S+/, '') + ' language-' + prismName).trim();
      any = true;
      if (P && P.languages && P.languages[prismName]) P.highlightElement(code);
      else needLoad = true;
    });
    if (any && needLoad) {
      ensurePrism().then(() => {
        const Pr = window.Prism;
        if (!Pr || !Pr.highlightElement) return;
        preview.querySelectorAll('pre > code[class*="language-"]').forEach((code) => {
          if (!code.isConnected) return;
          const m = /(?:^|\s)language-(\S+)/.exec(code.className);
          if (m && Pr.languages[m[1].toLowerCase()]) Pr.highlightElement(code);
        });
      });
    }
  }

  // 상대 경로 이미지를 문서 폴더 기준 file:// 로 보정
  function rewriteImages() {
    if (!g_baseUrl) return;
    const imgs = preview.querySelectorAll('img');
    for (const img of imgs) {
      const raw = img.getAttribute('src') || '';
      if (/^([a-zA-Z][a-zA-Z0-9+.-]*:|\/\/|\/)/.test(raw)) continue;
      img.src = g_baseUrl + encodeURI(raw);
    }
  }

  // 첫 렌더 후 유휴 시간에 Prism 을 미리 받아 둔다(다음 코드블록 편집을 즉시화).
  // KaTeX(수식) 는 문서에 수식이 있을 때만 로드되도록 그대로 둔다.
  let prefetched = false;
  function schedulePrefetch() {
    if (prefetched) return;
    prefetched = true;
    const idle = window.requestIdleCallback || ((fn) => setTimeout(fn, 300));
    idle(() => { ensurePrism(); });
  }

  function render(text) {
    lastText = text;
    if (!md) { ensureMarkdownIt().then((m) => { if (m) render(lastText); }); return; }
    mathPending = false;
    let html = md.render(text);
    html = taskListPostProcess(html);
    preview.innerHTML = html;
    rewriteImages();
    highlightCode();
    if (mathPending && !window.katex) ensureKatex().then((k) => { if (k) render(lastText); });
    schedulePrefetch();
  }

  // 미리보기 링크는 기본 브라우저로
  preview.addEventListener('click', (e) => {
    const a = e.target.closest && e.target.closest('a[href]');
    if (!a) return;
    const href = a.getAttribute('href') || '';
    if (href.startsWith('#')) {  // 내부 헤더 링크: 해당 헤더로 스크롤(목차)
      e.preventDefault();
      let id = href.slice(1);
      try { id = decodeURIComponent(id); } catch (err) {}  // href 는 퍼센트 인코딩됨
      const el = id && document.getElementById(id);
      if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
      return;
    }
    e.preventDefault();
    if (/^(https?:|mailto:)/i.test(href) && typeof window.mymdOpenExternal === 'function') {
      window.mymdOpenExternal(b64e(a.href || href));
    }
  });

  // ---------------------------------------------------------------------------
  // 네이티브 호스트 API
  // ---------------------------------------------------------------------------
  window.mymdRender = function (b64) { render(b64d(b64)); };
  window.mymdSetTheme = function (theme) { document.documentElement.dataset.theme = theme; };
  window.mymdSetBase = function (b64) { g_baseUrl = b64d(b64); rewriteImages(); };
  window.mymdSetLangs = function (b64) {
    try { const a = JSON.parse(b64d(b64)); if (Array.isArray(a)) g_langs = a; } catch (e) {}
  };
  window.mymdSetKeymap = function (b64) {
    try { const a = JSON.parse(b64d(b64)); if (Array.isArray(a)) g_keymap = a; } catch (e) {}
  };
  window.mymdScrollTo = function (ratio) {
    const max = preview.scrollHeight - preview.clientHeight;
    if (max > 0) preview.scrollTop = Math.round(max * ratio);
  };
  // 글자 배율(%) - 에디터와 함께 Ctrl +/- 로 조절. 본문 기준 15px(app.css)에 배율 적용.
  // 본문 요소가 모두 em 단위라 폰트 크기만 바꿔도 제목/코드/표/수식이 함께 확대된다.
  window.mymdSetZoom = function (percent) {
    let z = parseInt(percent, 10);
    if (!isFinite(z)) z = 100;
    z = Math.max(50, Math.min(300, z));
    preview.style.fontSize = (15 * z / 100) + 'px';
  };

  // 미리보기(WebView2)에 포커스가 있으면 호스트 ACCEL 이 키를 못 받으므로, 앱 단축키를
  // 여기서 가로채 id 로 호스트에 전달한다. 매핑은 호스트가 mymdSetKeymap 으로 보낸 keymap
  // (사용자 재바인딩 반영)을 따른다.
  document.addEventListener('keydown', (e) => {
    if (!e.ctrlKey && !e.altKey) return;  // 수식어 없는 입력은 무시
    const k = e.key.toLowerCase();
    for (const b of g_keymap) {
      if (!!b.ctrl === e.ctrlKey && !!b.shift === e.shiftKey &&
          !!b.alt === e.altKey && b.key === k) {
        e.preventDefault();
        if (typeof window.mymdAccel === 'function') window.mymdAccel(b.id);
        return;
      }
    }
  });

  // 첫 렌더 콜드 스타트 단축: markdown-it 가 정적 로드되어 즉시 빌드 가능하므로 미리 만들어 둔다.
  ensureMarkdownIt();

  // 준비 완료를 호스트에 알림(초기 상태를 받기 위함)
  if (typeof window.mymdPreviewReady === 'function') window.mymdPreviewReady();
})();
