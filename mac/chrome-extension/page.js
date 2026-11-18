(() => {
  if (globalThis.annaPage) return;

  const labels = element => [
    element.getAttribute('aria-label'), element.getAttribute('title'),
    element.getAttribute('data-tooltip-content')
  ].filter(Boolean).map(label => label.trim().toLowerCase());
  const named = (element, names) => labels(element).some(label => names.includes(label));
  const visible = element => element.getClientRects().length > 0 &&
    getComputedStyle(element).visibility === 'visible' && !element.closest('[aria-hidden="true"],[inert]');
  const buttons = names => [...document.querySelectorAll('button,[role="button"]')].filter(
    element => visible(element) && !element.matches(':disabled,[aria-disabled="true"]') &&
      (named(element, names) || names.includes(element.textContent.trim().toLowerCase()))
  );
  const dial = () => buttons(['发起通话', '呼叫', 'start call', '通話を開始', 'start a call', 'call']);
  const hangup = () => buttons(['挂断', 'hang up', '通話を終了', 'end call']);
  const send = () => buttons(['发送', 'send', 'send message', 'メッセージを送信']);
  const editors = () => [...document.querySelectorAll('textarea,input,[contenteditable="true"]')].filter(
    element => visible(element) && (named(element, ['消息', 'message', 'メッセージ', '发送消息', 'send a message']) ||
      ['消息', 'message', 'メッセージ', '发送消息', 'send a message'].includes(
        element.getAttribute('placeholder')?.trim().toLowerCase())));
  const value = editor => (editor.value ?? editor.innerText ?? editor.textContent).trim();

  function edit(editor, text) {
    editor.focus({preventScroll: true});
    if (editor.isContentEditable) {
      const range = document.createRange();
      range.selectNodeContents(editor);
      const selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      document.execCommand(text ? 'insertText' : 'delete', false, text);
      return;
    }
    const set = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(editor), 'value').set;
    set.call(editor, text);
    editor.dispatchEvent(new InputEvent('input', {bubbles: true, inputType: 'insertText', data: text}));
  }

  function clearTurn(turn) {
    if (turn?.editor?.isConnected && !turn.sent && value(turn.editor) === turn.text) {
      edit(turn.editor, '');
    }
  }

  function messages(limit = Infinity) {
    const groups = [...document.querySelectorAll('[aria-label],[title]')].filter(
      element => named(element, ['消息操作', 'message actions', 'メッセージの操作']));
    return groups.slice(-limit).flatMap(actions => {
      const row = actions.parentElement;
      if (!row || !visible(row)) return [];
      const copy = row.cloneNode(true);
      for (const element of copy.querySelectorAll('[aria-label],[title],button,[aria-hidden="true"],script')) {
        if (named(element, ['消息操作', 'message actions', 'メッセージの操作']) || element.matches('button,[aria-hidden="true"],script')) {
          element.remove();
        }
      }
      const text = copy.textContent.trim();
      const first = actions.querySelector('button,[role="button"]');
      const id = row.closest('[data-message-id]')?.getAttribute('data-message-id') ?? '';
      return text ? [{id, text, outgoing: !!first && named(first, ['回复', 'reply', '返信'])}] : [];
    });
  }

  const isProgress = text => text.length <= 180 &&
    /^(?:请\s*)?(?:稍等|稍候|等我|(?:我)?正在(?:处理|查询|核对)|我先(?:查|看|核|去)|give me a moment|one moment|working on it)/i.test(text) &&
    text.split(/[。！？!?\n]/).filter(part => part.trim()).length === 1;

  let turn;

  function snapshot(target, items = null, first = -1, end = -1) {
    if (location.href !== target) return null;
    const rows = items ?? messages(24);
    if (!rows.length && editors().length !== 1) return null;
    const offset = Math.max(0, rows.length - 20);
    return {
      rows: rows.slice(offset).map(({id, outgoing, text}) => [id, outgoing, text.slice(0, 6000)]),
      covered: rows.slice(offset).flatMap((_, i) =>
        i + offset >= first && i + offset < end ? [i] : [])
    };
  }

  function step(target, job) {
    if (location.href !== target) return {ok: false, message: chrome.i18n.getMessage('tab_changed')};
    if (!job) {
      clearTurn(turn);
      turn = undefined;
      return null;
    }
    const now = Date.now() / 1000;
    if (now >= job.expires) return null;
    if (job.action === 'dial') {
      const controls = dial();
      if (hangup().length || !controls.length || controls.length > 2) {
        return {ok: false, message: chrome.i18n.getMessage('cannot_dial')};
      }
      controls[0].click();
      return {ok: true};
    }
    if (job.action === 'hangup') {
      const controls = hangup();
      if (controls.length === 1) {
        controls[0].click();
        return {ok: true};
      }
      if (!controls.length && dial().length && now - job.dialedAt > 4) return {ok: true};
      return null;
    }
    if (turn?.id !== job.id) {
      clearTurn(turn);
      const inputs = editors();
      if (hangup().length || inputs.length !== 1 || value(inputs[0])) {
        return {ok: false, message: chrome.i18n.getMessage('draft')};
      }
      const baseline = messages().filter(item => item.outgoing && item.text === job.text).length;
      turn = {id: job.id, text: job.text, editor: inputs[0], baseline, changed: now};
      edit(turn.editor, turn.text);
    }
    if (!turn.sent) {
      const inputs = editors();
      const controls = send();
      if (inputs.length === 1 && value(inputs[0]) === turn.text && controls.length === 1) {
        turn.editor = inputs[0];
        controls[0].click();
        turn.sent = true;
      } else if (now - turn.changed > 5) {
        return {ok: false, message: chrome.i18n.getMessage('cannot_send')};
      }
      return null;
    }
    const items = messages();
    const anchors = items.flatMap((item, index) => item.outgoing && item.text === job.text ? [index] : []);
    if (anchors.length <= turn.baseline) return null;
    if (!turn.acknowledged) {
      turn.acknowledged = true;
      return {ok: true, partial: true, submitted: true};
    }
    const first = anchors.at(-1) + 1;
    const replies = [];
    for (const item of items.slice(first)) {
      if (item.outgoing) break;
      replies.push(item.text);
    }
    if (!replies.length) return null;
    const reply = replies.join('\n\n').slice(0, 1800);
    if (reply !== turn.observed) {
      turn.observed = reply;
      turn.changed = now;
    }
    const progress = isProgress(replies.at(-1));
    if (job.expires - now < 1.5 || now - turn.changed >= (progress ? 8 : .6)) {
      return {ok: true, reply, snapshot: snapshot(target, items, first, first + replies.length)};
    }
    if (reply !== turn.published) {
      turn.published = reply;
      return {ok: true, partial: true, preview: !progress, reply,
        snapshot: snapshot(target, items, first, first + replies.length)};
    }
    return null;
  }

  globalThis.annaPage = {step, snapshot};
})();
