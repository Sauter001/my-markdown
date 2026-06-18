/* MyMD - 프런트엔드 로직
 *
 * - C++ 호스트와 base64 기반 브리지로 파일 입출력/설정을 처리한다.
 * - markdown-it + KaTeX로 즉시 렌더링한다.
 * - 보기 모드, 단축키, 설정은 settings.json에 영속화한다.
 * - 브라우저에서 직접 열어도(브리지 부재) 편집/미리보기는 동작한다.
 */
(function () {
  'use strict';

  // ---------------------------------------------------------------------------
  // base64 (UTF-8 안전)
  // ---------------------------------------------------------------------------
  function b64e(str) {
    const bytes = new TextEncoder().encode(str);
    let bin = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      bin += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(bin);
  }
  function b64d(b64) {
    if (!b64) return '';
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new TextDecoder().decode(bytes);
  }

  // ---------------------------------------------------------------------------
  // 호스트 브리지 (없으면 안전한 폴백)
  // ---------------------------------------------------------------------------
  const inWebview = typeof window.mymdSave === 'function';
  const bridge = {
    ready:        () => inWebview ? window.mymdReady() : Promise.resolve({}),
    open:         () => inWebview ? window.mymdOpen()  : Promise.resolve({ cancelled: true }),
    save:    (t)  => inWebview ? window.mymdSave(b64e(t))   : Promise.resolve({ ok: true }),
    saveAs:  (t)  => inWebview ? window.mymdSaveAs(b64e(t)) : Promise.resolve({ cancelled: true }),
    newDoc:       () => inWebview ? window.mymdNew() : Promise.resolve({ ok: true }),
    setDirty:(d)  => inWebview ? window.mymdSetDirty(!!d) : Promise.resolve(true),
    loadSettings: () => inWebview ? window.mymdLoadSettings() : Promise.resolve({}),
    saveSettings: (t) => inWebview ? window.mymdSaveSettings(b64e(t)) : Promise.resolve(true),
    dragMove:  () => inWebview ? window.mymdDragMove()  : Promise.resolve(true),
    minimize:  () => inWebview ? window.mymdMinimize()  : Promise.resolve(true),
    toggleMax: () => inWebview ? window.mymdToggleMax() : Promise.resolve(true),
    closeWin:  () => inWebview ? window.mymdClose()     : Promise.resolve(true),
  };

  // ---------------------------------------------------------------------------
  // markdown-it + KaTeX
  // ---------------------------------------------------------------------------
  const md = window.markdownit({
    html: true, linkify: true, typographer: true, breaks: false,
  });
  applyKatex(md);
  // 로컬 에디터이므로 data:(SVG 포함), file: 등 모든 링크를 허용.
  // (markdown-it 기본 검증은 data:image/svg+xml, file: 등을 차단한다.)
  md.validateLink = function () { return true; };

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
        token.markup = '$';
        token.content = state.src.slice(start, match);
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
          lastLine = state.src.slice(pos, lastPos);
          found = true;
        }
      }
      state.line = next + 1;
      const token = state.push('math_block', 'math', 0);
      token.block = true;
      token.content = (firstLine && firstLine.trim() ? firstLine + '\n' : '')
        + state.getLines(start + 1, next, state.tShift[start], true)
        + (lastLine && lastLine.trim() ? lastLine : '');
      token.map = [start, state.line];
      token.markup = '$$';
      return true;
    }
    const renderInline = (latex) => {
      try { return window.katex.renderToString(latex, { displayMode: false, throwOnError: false }); }
      catch (e) { return escapeHtml(latex); }
    };
    const renderBlock = (latex) => {
      try { return "<p class='katex-block'>" + window.katex.renderToString(latex, { displayMode: true, throwOnError: false }) + '</p>'; }
      catch (e) { return '<p class="katex-block">' + escapeHtml(latex) + '</p>'; }
    };
    md.inline.ruler.after('escape', 'math_inline', mathInline);
    md.block.ruler.after('blockquote', 'math_block', mathBlock,
      { alt: ['paragraph', 'reference', 'blockquote', 'list'] });
    md.renderer.rules.math_inline = (tokens, idx) => renderInline(tokens[idx].content);
    md.renderer.rules.math_block = (tokens, idx) => renderBlock(tokens[idx].content) + '\n';
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }

  // 작업목록(task list) 가벼운 후처리
  function taskListPostProcess(html) {
    return html
      .replace(/<li>\s*\[ \]\s*/g, '<li class="task-list-item"><input class="task-list-item-checkbox" type="checkbox" disabled> ')
      .replace(/<li>\s*\[[xX]\]\s*/g, '<li class="task-list-item"><input class="task-list-item-checkbox" type="checkbox" checked disabled> ');
  }

  // ---------------------------------------------------------------------------
  // DOM 참조
  // ---------------------------------------------------------------------------
  const editor = document.getElementById('editor');
  const preview = document.getElementById('preview');
  const previewPane = document.getElementById('previewPane');
  const editorPane = document.getElementById('editorPane');
  const divider = document.getElementById('divider');
  const filenameEl = document.getElementById('filename');
  const dirtyDot = document.getElementById('dirtyDot');
  const segBtns = Array.prototype.slice.call(document.querySelectorAll('.seg-btn'));

  // ---------------------------------------------------------------------------
  // 상태
  // ---------------------------------------------------------------------------
  let currentName = '';
  let currentPath = '';
  let savedText = '';
  let dirty = false;
  let settings = null;
  let renderTimer = null;

  // ---------------------------------------------------------------------------
  // 렌더링
  // ---------------------------------------------------------------------------
  function render() {
    let html = md.render(editor.value);
    html = taskListPostProcess(html);
    preview.innerHTML = html;
    rewriteImages();
  }
  function scheduleRender() {
    if (document.body.dataset.view === 'editor') return;
    clearTimeout(renderTimer);
    renderTimer = setTimeout(render, 110);
  }

  function dirUrlOfCurrent() {
    if (!currentPath) return '';
    let p = currentPath.replace(/[\\/][^\\/]*$/, '').replace(/\\/g, '/');
    if (!p) return '';
    return 'file:///' + encodeURI(p) + '/';
  }
  function rewriteImages() {
    const base = dirUrlOfCurrent();
    if (!base) return;
    const imgs = preview.querySelectorAll('img');
    for (const img of imgs) {
      const raw = img.getAttribute('src') || '';
      if (/^([a-zA-Z][a-zA-Z0-9+.-]*:|\/\/|\/)/.test(raw)) continue; // 절대 경로/스킴
      img.src = base + encodeURI(raw);
    }
  }

  // ---------------------------------------------------------------------------
  // 더티 상태 / 파일명
  // ---------------------------------------------------------------------------
  function setFilename() {
    filenameEl.textContent = currentName || '제목 없음';
  }
  function updateDirty() {
    const d = editor.value !== savedText;
    if (d !== dirty) {
      dirty = d;
      dirtyDot.hidden = !d;
      bridge.setDirty(d);
    }
  }
  async function confirmDiscard() {
    if (!dirty) return true;
    return window.confirm('저장하지 않은 변경사항이 있습니다. 계속하시겠습니까?');
  }

  // ---------------------------------------------------------------------------
  // 파일 동작
  // ---------------------------------------------------------------------------
  async function doNew() {
    if (!(await confirmDiscard())) return;
    await bridge.newDoc();
    currentName = ''; currentPath = ''; editor.value = ''; savedText = '';
    setFilename(); render(); updateDirty(); editor.focus();
  }
  async function doOpen() {
    if (!(await confirmDiscard())) return;
    const r = await bridge.open();
    if (!r || r.cancelled || r.error) return;
    currentName = b64d(r.nameB64); currentPath = b64d(r.pathB64);
    editor.value = b64d(r.contentB64); savedText = editor.value;
    setFilename(); render(); updateDirty(); editor.focus();
  }
  async function doSave() {
    const r = await bridge.save(editor.value);
    if (r && r.needSaveAs) return doSaveAs();
    if (r && r.ok) { savedText = editor.value; updateDirty(); }
    return r;
  }
  async function doSaveAs() {
    const r = await bridge.saveAs(editor.value);
    if (!r || r.cancelled) return;
    if (r.ok) {
      currentName = b64d(r.nameB64); currentPath = b64d(r.pathB64);
      savedText = editor.value; setFilename(); updateDirty();
    }
  }

  // ---------------------------------------------------------------------------
  // 보기 모드
  // ---------------------------------------------------------------------------
  function setView(v) {
    document.body.dataset.view = v;
    segBtns.forEach((b) => b.classList.toggle('active', b.dataset.view === v));
    if (v !== 'editor') render();
  }
  function cycleView() {
    const order = ['editor', 'split', 'preview'];
    const i = order.indexOf(document.body.dataset.view);
    setView(order[(i + 1) % order.length]);
  }

  // ---------------------------------------------------------------------------
  // 설정
  // ---------------------------------------------------------------------------
  const DEFAULT_SETTINGS = {
    defaultView: 'split',
    theme: 'system',
    fontSize: 14,
    tabSize: 4,
    wrap: true,
    scrollSync: true,
    keymap: {
      save: 'Ctrl+S', saveAs: 'Ctrl+Shift+S', open: 'Ctrl+O', new: 'Ctrl+N',
      viewEditor: 'Ctrl+1', viewSplit: 'Ctrl+2', viewPreview: 'Ctrl+3',
      cycleView: 'Ctrl+\\', settings: 'Ctrl+,',
      insertTable: 'Ctrl+T', formatTable: 'Ctrl+Shift+F',
    },
  };
  const KEYMAP_ACTIONS = [
    { id: 'save', label: '저장', run: doSave },
    { id: 'saveAs', label: '다른 이름으로 저장', run: doSaveAs },
    { id: 'open', label: '열기', run: doOpen },
    { id: 'new', label: '새 파일', run: doNew },
    { id: 'viewEditor', label: '에디터 보기', run: () => setView('editor') },
    { id: 'viewSplit', label: '병치 보기', run: () => setView('split') },
    { id: 'viewPreview', label: '미리보기 보기', run: () => setView('preview') },
    { id: 'cycleView', label: '보기 순환', run: cycleView },
    { id: 'insertTable', label: '표 삽입', run: () => insertTableQuick() },
    { id: 'formatTable', label: '표 정렬', run: () => formatTableAtCaret() },
    { id: 'settings', label: '설정 열기', run: openSettings },
  ];

  let mediaQuery = null;
  function resolveTheme(theme) {
    if (theme === 'system') {
      return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    return theme;
  }
  function applySettings(s) {
    document.documentElement.dataset.theme = resolveTheme(s.theme);
    editor.style.fontSize = s.fontSize + 'px';
    editor.style.tabSize = String(s.tabSize);
    editor.classList.toggle('nowrap', !s.wrap);
    // 시스템 테마 변경 추적
    if (mediaQuery) mediaQuery.onchange = null;
    if (s.theme === 'system') {
      mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
      mediaQuery.onchange = () => { document.documentElement.dataset.theme = resolveTheme('system'); };
    }
  }
  async function loadSettings() {
    const r = await bridge.loadSettings();
    let s = JSON.parse(JSON.stringify(DEFAULT_SETTINGS));
    if (r && r.b64) {
      try {
        const obj = JSON.parse(b64d(r.b64));
        s = Object.assign(s, obj);
        s.keymap = Object.assign(JSON.parse(JSON.stringify(DEFAULT_SETTINGS.keymap)), obj.keymap || {});
      } catch (e) { /* 손상된 설정은 무시 */ }
    }
    return s;
  }
  function persistSettings() { return bridge.saveSettings(JSON.stringify(settings)); }

  // 설정 모달
  const overlay = document.getElementById('settingsOverlay');
  let settingsOpen = false;
  let captureTarget = null;          // 캡처 중인 단축키 액션 id
  let pendingKeymap = null;          // 모달 작업용 키맵 사본
  const keymapButtons = {};

  function openSettings() {
    pendingKeymap = JSON.parse(JSON.stringify(settings.keymap));
    document.getElementById('set-defaultView').value = settings.defaultView;
    document.getElementById('set-theme').value = settings.theme;
    document.getElementById('set-fontSize').value = settings.fontSize;
    document.getElementById('set-tabSize').value = settings.tabSize;
    document.getElementById('set-wrap').checked = settings.wrap;
    document.getElementById('set-scrollSync').checked = settings.scrollSync;
    buildKeymapList();
    overlay.hidden = false;
    settingsOpen = true;
  }
  function closeSettings() {
    overlay.hidden = true;
    settingsOpen = false;
    captureTarget = null;
  }
  function buildKeymapList() {
    const list = document.getElementById('keymapList');
    list.innerHTML = '';
    for (const act of KEYMAP_ACTIONS) {
      const row = document.createElement('div');
      row.className = 'keymap-row';
      const label = document.createElement('span');
      label.className = 'km-label';
      label.textContent = act.label;
      const key = document.createElement('button');
      key.className = 'km-key';
      key.textContent = pendingKeymap[act.id] || '(없음)';
      key.onclick = () => {
        if (captureTarget && keymapButtons[captureTarget]) keymapButtons[captureTarget].classList.remove('capturing');
        captureTarget = act.id;
        key.classList.add('capturing');
        key.textContent = '키 입력...';
      };
      keymapButtons[act.id] = key;
      row.appendChild(label);
      row.appendChild(key);
      list.appendChild(row);
    }
  }
  function saveSettingsFromModal() {
    settings.defaultView = document.getElementById('set-defaultView').value;
    settings.theme = document.getElementById('set-theme').value;
    settings.fontSize = clampNum(document.getElementById('set-fontSize').value, 10, 32, 14);
    settings.tabSize = clampNum(document.getElementById('set-tabSize').value, 1, 8, 4);
    settings.wrap = document.getElementById('set-wrap').checked;
    settings.scrollSync = document.getElementById('set-scrollSync').checked;
    settings.keymap = pendingKeymap;
    applySettings(settings);
    setView(settings.defaultView);
    persistSettings();
    closeSettings();
  }
  function clampNum(v, lo, hi, dft) {
    let n = parseInt(v, 10);
    if (isNaN(n)) return dft;
    return Math.max(lo, Math.min(hi, n));
  }

  // ---------------------------------------------------------------------------
  // 단축키 / 캡처
  // ---------------------------------------------------------------------------
  function comboFromEvent(e) {
    const mods = [];
    if (e.ctrlKey) mods.push('Ctrl');
    if (e.altKey) mods.push('Alt');
    if (e.shiftKey) mods.push('Shift');
    if (e.metaKey) mods.push('Meta');
    let key = e.key;
    if (key === 'Control' || key === 'Alt' || key === 'Shift' || key === 'Meta') return null;
    if (key === ' ') key = 'Space';
    if (key.length === 1) key = key.toUpperCase();
    return mods.concat(key).join('+');
  }
  document.addEventListener('keydown', (e) => {
    // 단축키 캡처 모드
    if (captureTarget) {
      const combo = comboFromEvent(e);
      if (!combo) return;
      e.preventDefault(); e.stopPropagation();
      pendingKeymap[captureTarget] = combo;
      const btn = keymapButtons[captureTarget];
      if (btn) { btn.textContent = combo; btn.classList.remove('capturing'); }
      captureTarget = null;
      return;
    }
    if (settingsOpen || tableDialogOpen) {
      if (e.key === 'Escape') { e.preventDefault(); if (settingsOpen) closeSettings(); else closeTableDialog(); }
      return;
    }
    const combo = comboFromEvent(e);
    if (!combo) return;
    for (const act of KEYMAP_ACTIONS) {
      const binding = settings && settings.keymap[act.id];
      if (binding && binding.toLowerCase() === combo.toLowerCase()) {
        e.preventDefault(); e.stopPropagation();
        act.run();
        return;
      }
    }
  }, true);

  // ---------------------------------------------------------------------------
  // 에디터 동작
  // ---------------------------------------------------------------------------
  editor.addEventListener('input', () => { updateDirty(); scheduleRender(); });
  editor.addEventListener('keydown', (e) => {
    if (e.isComposing) return;
    if (e.key === 'Tab') {
      if (tableNav(e.shiftKey)) { e.preventDefault(); return; }
      if (e.shiftKey) return; // 표 밖에서 Shift+Tab은 기본 동작
      e.preventDefault();
      const spaces = ' '.repeat(settings ? settings.tabSize : 4);
      const s = editor.selectionStart, en = editor.selectionEnd;
      editor.value = editor.value.slice(0, s) + spaces + editor.value.slice(en);
      editor.selectionStart = editor.selectionEnd = s + spaces.length;
      updateDirty(); scheduleRender();
    } else if (e.key === 'Enter' && !e.shiftKey) {
      if (tableEnter()) e.preventDefault();
    }
  });

  // 괄호/따옴표/강조 자동 페어링
  const PAIRS = { '(': ')', '[': ']', '{': '}', '"': '"', "'": "'", '`': '`', '*': '*' };
  const SYMMETRIC = new Set(['"', "'", '`', '*']);
  const CLOSERS = new Set([')', ']', '}']);
  editor.addEventListener('keydown', (e) => {
    if (e.isComposing || e.keyCode === 229) return; // 한글 조합 중에는 무시
    if (e.ctrlKey || e.altKey || e.metaKey) return; // 단축키 제외 (Shift는 괄호 입력에 필요하므로 허용)
    const key = e.key;
    const val = editor.value;
    const s = editor.selectionStart, en = editor.selectionEnd;
    const hasSel = s !== en;

    // ** 강조: *|* 사이에서 * 입력 시 **|** 로 확장
    if (key === '*' && !hasSel && val[s - 1] === '*' && val[s] === '*') {
      e.preventDefault();
      document.execCommand('insertText', false, '**');
      editor.setSelectionRange(s + 1, s + 1);
      return;
    }

    // 여는 괄호 또는 대칭 문자
    if (Object.prototype.hasOwnProperty.call(PAIRS, key)) {
      const close = PAIRS[key];
      // 대칭 문자 스킵 오버: 선택이 없고 바로 다음 글자가 같은 문자면 통과
      if (SYMMETRIC.has(key) && !hasSel && val[s] === key) {
        e.preventDefault();
        editor.setSelectionRange(s + 1, s + 1);
        return;
      }
      // 따옴표 예외: 영문/숫자/한글 바로 뒤의 ' 또는 " 는 페어링하지 않음(축약형 등)
      if ((key === '"' || key === "'") && !hasSel) {
        const prev = val[s - 1];
        if (prev && /[A-Za-z0-9_가-힣]/.test(prev)) return;
      }
      e.preventDefault();
      if (hasSel) {
        const sel = val.slice(s, en);
        document.execCommand('insertText', false, key + sel + close);
        editor.setSelectionRange(s + 1, s + 1 + sel.length); // 안쪽 텍스트 재선택
      } else {
        document.execCommand('insertText', false, key + close);
        editor.setSelectionRange(s + 1, s + 1);
      }
      return;
    }

    // 닫는 괄호 스킵 오버
    if (CLOSERS.has(key) && !hasSel && val[s] === key) {
      e.preventDefault();
      editor.setSelectionRange(s + 1, s + 1);
      return;
    }

    // 빈 짝에서 Backspace 시 양쪽 함께 삭제
    if (key === 'Backspace' && !hasSel && s > 0) {
      const open = val[s - 1];
      if (PAIRS[open] && PAIRS[open] === val[s]) {
        e.preventDefault();
        editor.setSelectionRange(s - 1, s + 1);
        document.execCommand('delete');
        return;
      }
    }
  });
  editor.addEventListener('scroll', () => {
    if (!settings || !settings.scrollSync || document.body.dataset.view !== 'split') return;
    const er = editor.scrollHeight - editor.clientHeight;
    if (er <= 0) return;
    const ratio = editor.scrollTop / er;
    const pr = previewPane.scrollHeight - previewPane.clientHeight;
    previewPane.scrollTop = ratio * pr;
  });

  // 분할 너비 드래그
  (function () {
    let dragging = false;
    divider.addEventListener('mousedown', (e) => { dragging = true; document.body.style.cursor = 'col-resize'; e.preventDefault(); });
    window.addEventListener('mousemove', (e) => {
      if (!dragging) return;
      const ws = document.getElementById('workspace');
      const rect = ws.getBoundingClientRect();
      let pct = ((e.clientX - rect.left) / rect.width) * 100;
      pct = Math.max(15, Math.min(85, pct));
      editorPane.style.flex = '0 0 ' + pct + '%';
      previewPane.style.flex = '1 1 auto';
    });
    window.addEventListener('mouseup', () => { dragging = false; document.body.style.cursor = ''; });
  })();

  // ---------------------------------------------------------------------------
  // 표 편의 기능 (정렬, Tab/Enter 셀 이동/행 추가, 삽입)
  // ---------------------------------------------------------------------------
  function tblGetLines(val) {
    const out = []; let i = 0; const parts = val.split('\n');
    for (let k = 0; k < parts.length; k++) { out.push({ text: parts[k], start: i }); i += parts[k].length + 1; }
    return out;
  }
  function tblLineIndexAt(lines, pos) {
    for (let k = 0; k < lines.length; k++) { const L = lines[k]; if (pos >= L.start && pos <= L.start + L.text.length) return k; }
    return lines.length - 1;
  }
  function tblIsRow(text) {
    const t = text.trim(); if (!t.includes('|')) return false;
    return t.startsWith('|') || (t.match(/\|/g) || []).length >= 2;
  }
  function tblParseCells(line) {
    let t = line.trim(); if (t.startsWith('|')) t = t.slice(1); if (t.endsWith('|')) t = t.slice(0, -1);
    return t.split('|').map((c) => c.trim());
  }
  function tblIsSep(text) {
    const cells = tblParseCells(text);
    return cells.length > 0 && cells.every((c) => /^:?-{1,}:?$/.test(c.trim()));
  }
  function tblPipes(text) {
    const p = []; for (let i = 0; i < text.length; i++) { if (text[i] === '|' && text[i - 1] !== '\\') p.push(i); }
    return p;
  }
  function tblBounds(lines, idx) {
    let top = idx, bot = idx;
    while (top > 0 && tblIsRow(lines[top - 1].text)) top--;
    while (bot < lines.length - 1 && tblIsRow(lines[bot + 1].text)) bot++;
    return { top, bot };
  }
  function tblChW(ch) {
    const c = ch.codePointAt(0);
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x20000 && c <= 0x3FFFD)) return 2;
    return 1;
  }
  function tblW(s) { let w = 0; for (const ch of s) w += tblChW(ch); return w; }
  function tblPad(s, n) { return s + ' '.repeat(Math.max(0, n - tblW(s))); }
  function tblPadC(s, n) { const t = Math.max(0, n - tblW(s)); const l = Math.floor(t / 2); return ' '.repeat(l) + s + ' '.repeat(t - l); }
  function tblReplace(a, b, text) { editor.focus(); editor.setSelectionRange(a, b); document.execCommand('insertText', false, text); }
  function tblSelectCell(line, cellIdx) {
    const pipes = tblPipes(line.text);
    if (cellIdx < 0 || cellIdx > pipes.length - 2) return false;
    const cs = pipes[cellIdx] + 1, ce = pipes[cellIdx + 1];
    let s = cs; while (s < ce && line.text[s] === ' ') s++;
    let e = ce; while (e > s && line.text[e - 1] === ' ') e--;
    editor.setSelectionRange(line.start + s, line.start + e);
    return true;
  }
  function tblNextRow(lines, from, bot) { for (let k = from; k <= bot; k++) { if (!tblIsSep(lines[k].text)) return k; } return -1; }
  function tblPrevRow(lines, from, top) { for (let k = from; k >= top; k--) { if (!tblIsSep(lines[k].text)) return k; } return -1; }

  function formatTableAtCaret() {
    const val = editor.value; const lines = tblGetLines(val);
    const ci = tblLineIndexAt(lines, editor.selectionStart);
    if (!tblIsRow(lines[ci].text)) return false;
    const { top, bot } = tblBounds(lines, ci);
    const rows = []; let sepAt = -1;
    for (let k = top; k <= bot; k++) {
      const sep = tblIsSep(lines[k].text);
      if (sep && sepAt < 0) sepAt = rows.length;
      rows.push({ cells: tblParseCells(lines[k].text), sep });
    }
    const cols = Math.max.apply(null, rows.map((r) => r.cells.length));
    const align = new Array(cols).fill('none');
    if (sepAt >= 0) {
      const sc = rows[sepAt].cells;
      for (let c = 0; c < cols; c++) {
        const x = (sc[c] || '').trim(); const L = x.startsWith(':'), R = x.endsWith(':');
        align[c] = L && R ? 'center' : R ? 'right' : L ? 'left' : 'none';
      }
    }
    const width = new Array(cols).fill(3);
    for (const r of rows) { if (r.sep) continue; for (let c = 0; c < cols; c++) width[c] = Math.max(width[c], tblW(r.cells[c] || '')); }
    const out = [];
    for (const r of rows) {
      const segs = [];
      if (r.sep) {
        for (let c = 0; c < cols; c++) {
          const w = width[c]; let d;
          if (align[c] === 'center') d = ':' + '-'.repeat(Math.max(1, w - 2)) + ':';
          else if (align[c] === 'right') d = '-'.repeat(Math.max(2, w - 1)) + ':';
          else if (align[c] === 'left') d = ':' + '-'.repeat(Math.max(2, w - 1));
          else d = '-'.repeat(Math.max(3, w));
          segs.push(d);
        }
      } else {
        for (let c = 0; c < cols; c++) {
          const cell = r.cells[c] || '';
          segs.push(align[c] === 'right' ? ' '.repeat(Math.max(0, width[c] - tblW(cell))) + cell
                  : align[c] === 'center' ? tblPadC(cell, width[c])
                  : tblPad(cell, width[c]));
        }
      }
      out.push('| ' + segs.join(' | ') + ' |');
    }
    if (sepAt < 0) {
      const segs = []; for (let c = 0; c < cols; c++) segs.push('-'.repeat(Math.max(3, width[c])));
      out.splice(1, 0, '| ' + segs.join(' | ') + ' |');
    }
    const blockStart = lines[top].start;
    const blockEnd = lines[bot].start + lines[bot].text.length;
    tblReplace(blockStart, blockEnd, out.join('\n'));
    editor.setSelectionRange(blockStart, blockStart);
    return true;
  }

  function insertTableSkeleton(cols, rows) {
    const header = '| ' + Array.from({ length: cols }, (_, i) => '제목' + (i + 1)).join(' | ') + ' |';
    const sep = '| ' + Array(cols).fill('---').join(' | ') + ' |';
    const body = [];
    for (let r = 0; r < rows; r++) body.push('| ' + Array(cols).fill('  ').join(' | ') + ' |');
    const pos = editor.selectionStart; const val = editor.value;
    const atStart = pos === 0 || val[pos - 1] === '\n';
    const prefix = atStart ? '' : '\n';
    const text = prefix + [header, sep].concat(body).join('\n') + '\n';
    tblReplace(pos, pos, text);
    const firstCell = pos + prefix.length + 2; // "| " 다음
    editor.setSelectionRange(firstCell, firstCell + '제목1'.length); // 첫 헤더 셀 선택
    editor.focus();
    return true;
  }
  function insertTableQuick() { return insertTableSkeleton(2, 2); }

  function tableEnter() {
    const val = editor.value; const pos = editor.selectionStart;
    if (pos !== editor.selectionEnd) return false;
    const lines = tblGetLines(val); const ci = tblLineIndexAt(lines, pos);
    if (!tblIsRow(lines[ci].text)) return false;
    const { top, bot } = tblBounds(lines, ci);
    let sepIdx = -1; for (let k = top; k <= bot; k++) { if (tblIsSep(lines[k].text)) { sepIdx = k; break; } }
    const cols = tblParseCells(lines[top].text).length;
    const emptyRow = '|' + Array(cols).fill('  ').join('|') + '|';
    let insertPos, insertText, rowStart;
    if (sepIdx < 0) {
      const sep = '| ' + Array(cols).fill('---').join(' | ') + ' |';
      insertPos = lines[ci].start + lines[ci].text.length;
      insertText = '\n' + sep + '\n' + emptyRow;
      rowStart = insertPos + 1 + sep.length + 1;
    } else {
      const anchor = (ci < sepIdx) ? sepIdx : ci;
      insertPos = lines[anchor].start + lines[anchor].text.length;
      insertText = '\n' + emptyRow;
      rowStart = insertPos + 1;
    }
    tblReplace(insertPos, insertPos, insertText);
    const caret = rowStart + 1; // 새 행 첫 셀
    editor.setSelectionRange(caret, caret);
    return true;
  }

  function tableAddColumn(lines, top, bot, ci) {
    let hasSep = false;
    const out = [];
    for (let k = top; k <= bot; k++) {
      const sep = tblIsSep(lines[k].text);
      if (sep) hasSep = true;
      let t = lines[k].text.trim();
      if (!t.startsWith('|')) t = '| ' + t;
      if (!t.endsWith('|')) t = t + ' |';
      out.push(sep ? (t + ' --- |') : (t + '   |')); // 빈 셀 추가
    }
    const blockStart = lines[top].start;
    const blockEnd = lines[bot].start + lines[bot].text.length;
    tblReplace(blockStart, blockEnd, out.join('\n'));
    if (hasSep) { editor.setSelectionRange(blockStart, blockStart); formatTableAtCaret(); }
    // 같은 행의 새(마지막) 셀 선택 (행 수가 그대로라 줄 번호 유지)
    const lines2 = tblGetLines(editor.value);
    const tgt = lines2[ci];
    if (tgt) { const pp = tblPipes(tgt.text); tblSelectCell(tgt, pp.length - 2); }
    return true;
  }

  function tableNav(shift) {
    const val = editor.value; const pos = editor.selectionStart;
    const lines = tblGetLines(val); const ci = tblLineIndexAt(lines, pos);
    if (!tblIsRow(lines[ci].text)) return false;
    const { top, bot } = tblBounds(lines, ci);
    const line = lines[ci]; const pipes = tblPipes(line.text); const col = pos - line.start;
    if (pipes.length < 2) return false;
    let cell = -1;
    for (let k = 0; k < pipes.length - 1; k++) { if (col >= pipes[k] && col <= pipes[k + 1]) { cell = k; break; } }
    if (cell < 0) { tblSelectCell(line, 0); return true; }
    if (!shift) {
      if (cell + 1 <= pipes.length - 2) { tblSelectCell(line, cell + 1); return true; }
      // 마지막 열에서 Tab: 열 추가 후 새 셀로 이동 (행 추가는 Enter가 담당)
      return tableAddColumn(lines, top, bot, ci);
    }
    if (cell - 1 >= 0) { tblSelectCell(line, cell - 1); return true; }
    const pr = tblPrevRow(lines, ci - 1, top);
    if (pr >= 0) { const pl = lines[pr]; const pp = tblPipes(pl.text); tblSelectCell(pl, pp.length - 2); return true; }
    return true;
  }

  // 표 삽입 대화상자
  const tableOverlay = document.getElementById('tableOverlay');
  let tableDialogOpen = false;
  function openTableDialog() {
    document.getElementById('tbl-cols').value = 2;
    document.getElementById('tbl-rows').value = 2;
    tableOverlay.hidden = false;
    tableDialogOpen = true;
  }
  function closeTableDialog() { tableOverlay.hidden = true; tableDialogOpen = false; }
  function insertFromDialog() {
    const cols = clampNum(document.getElementById('tbl-cols').value, 1, 20, 2);
    const rows = clampNum(document.getElementById('tbl-rows').value, 1, 50, 2);
    closeTableDialog();
    insertTableSkeleton(cols, rows);
  }

  // ---------------------------------------------------------------------------
  // UI 배선
  // ---------------------------------------------------------------------------
  function wireUI() {
    document.querySelectorAll('.tb-btn[data-act]').forEach((b) => {
      b.addEventListener('click', () => {
        const act = b.dataset.act;
        if (act === 'new') doNew();
        else if (act === 'open') doOpen();
        else if (act === 'save') doSave();
        else if (act === 'table') openTableDialog();
        else if (act === 'tableFmt') formatTableAtCaret();
        else if (act === 'settings') openSettings();
      });
    });
    segBtns.forEach((b) => b.addEventListener('click', () => setView(b.dataset.view)));

    // 프레임리스 창: 상단바 드래그 이동, 더블클릭 최대화, 창 제어 버튼
    const topbar = document.getElementById('topbar');
    const isInteractive = (t) => t.closest('button, .seg, .winctrl, input, select, a');
    topbar.addEventListener('mousedown', (e) => {
      if (e.button !== 0 || isInteractive(e.target)) return;
      bridge.dragMove();
    });
    topbar.addEventListener('dblclick', (e) => {
      if (isInteractive(e.target)) return;
      bridge.toggleMax();
    });
    document.querySelectorAll('.wc-btn[data-win]').forEach((b) => {
      b.addEventListener('click', () => {
        const w = b.dataset.win;
        if (w === 'min') bridge.minimize();
        else if (w === 'max') bridge.toggleMax();
        else if (w === 'close') bridge.closeWin();
      });
    });

    document.getElementById('set-ok').addEventListener('click', saveSettingsFromModal);
    document.getElementById('set-cancel').addEventListener('click', closeSettings);
    document.getElementById('set-reset').addEventListener('click', () => {
      pendingKeymap = JSON.parse(JSON.stringify(DEFAULT_SETTINGS.keymap));
      document.getElementById('set-defaultView').value = DEFAULT_SETTINGS.defaultView;
      document.getElementById('set-theme').value = DEFAULT_SETTINGS.theme;
      document.getElementById('set-fontSize').value = DEFAULT_SETTINGS.fontSize;
      document.getElementById('set-tabSize').value = DEFAULT_SETTINGS.tabSize;
      document.getElementById('set-wrap').checked = DEFAULT_SETTINGS.wrap;
      document.getElementById('set-scrollSync').checked = DEFAULT_SETTINGS.scrollSync;
      buildKeymapList();
    });
    overlay.addEventListener('mousedown', (e) => { if (e.target === overlay) closeSettings(); });

    document.getElementById('tbl-ok').addEventListener('click', insertFromDialog);
    document.getElementById('tbl-cancel').addEventListener('click', closeTableDialog);
    tableOverlay.addEventListener('mousedown', (e) => { if (e.target === tableOverlay) closeTableDialog(); });
  }

  // ---------------------------------------------------------------------------
  // 초기화
  // ---------------------------------------------------------------------------
  (async function init() {
    settings = await loadSettings();
    applySettings(settings);
    setView(settings.defaultView);
    wireUI();
    const r = await bridge.ready();
    if (r && r.ok) {
      currentName = b64d(r.nameB64); currentPath = b64d(r.pathB64);
      editor.value = b64d(r.contentB64); savedText = editor.value;
    } else {
      editor.value = ''; savedText = '';
    }
    setFilename(); render(); updateDirty(); editor.focus();
  })();
})();
