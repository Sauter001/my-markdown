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
    if (settingsOpen) {
      if (e.key === 'Escape') { e.preventDefault(); closeSettings(); }
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
    if (e.key === 'Tab') {
      e.preventDefault();
      const spaces = ' '.repeat(settings ? settings.tabSize : 4);
      const s = editor.selectionStart, en = editor.selectionEnd;
      editor.value = editor.value.slice(0, s) + spaces + editor.value.slice(en);
      editor.selectionStart = editor.selectionEnd = s + spaces.length;
      updateDirty(); scheduleRender();
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
  // UI 배선
  // ---------------------------------------------------------------------------
  function wireUI() {
    document.querySelectorAll('.tb-btn[data-act]').forEach((b) => {
      b.addEventListener('click', () => {
        const act = b.dataset.act;
        if (act === 'new') doNew();
        else if (act === 'open') doOpen();
        else if (act === 'save') doSave();
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
