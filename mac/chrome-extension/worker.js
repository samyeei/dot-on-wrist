const HOST = 'com.samyeei.anna_chrome';
let connection;

async function page(target, preferred) {
  const matches = (await chrome.tabs.query({url: target}))
    .filter(tab => tab.url === target && !tab.discarded);
  if (preferred !== undefined) return matches.find(tab => tab.id === preferred)?.id;
  return (matches.find(tab => tab.active) ?? matches.sort((a, b) => a.id - b.id)[0])?.id;
}

async function invoke(tabId, documentId, target, job) {
  const [execution] = await chrome.scripting.executeScript({
    target: {tabId, documentIds: [documentId]},
    func: (target, job) => globalThis.annaPage.step(target, job),
    args: [target, job]
  });
  return execution.result;
}

async function snapshot(tabId, documentId, target) {
  const [execution] = await chrome.scripting.executeScript({
    target: {tabId, documentIds: [documentId]},
    func: target => globalThis.annaPage.snapshot(target),
    args: [target]
  });
  return execution.result;
}

function connect() {
  if (connection) return;
  const port = chrome.runtime.connectNative(HOST);
  connection = port;
  let target, timer, running, last, ready, callTab;
  let scanAt = 0;

  port.onDisconnect.addListener(() => {
    const error = chrome.runtime.lastError?.message;
    if (error) console.warn('ANNA local bridge:', error);
    connection = undefined;
    clearTimeout(timer);
    chrome.action.setBadgeText({text: chrome.i18n.getMessage('badge_off')});
    setTimeout(connect, 2000);
  });

  async function poll(job) {
    if (connection !== port) return;
    let tabId, result, observed;
    try {
      tabId = await page(target, running?.id === job?.id && running ? running.tabId
        : job?.action === 'hangup' ? job.tabId ?? callTab : undefined);
      if (running && running.id !== job?.id) {
        await invoke(running.tabId, running.documentId, target, null).catch(() => {});
        running = undefined;
      }
      if (job) {
        if (last?.id === job.id) {
          result = last;
        } else if (tabId === undefined || (running && running.tabId !== tabId)) {
          result = {ok: false, message: chrome.i18n.getMessage('open_tab')};
        } else {
          if (!running) {
            // ANNA's live conversation may pause updates in an inactive tab.
            await chrome.tabs.update(tabId, {active: true});
            const [execution] = await chrome.scripting.executeScript({
              target: {tabId}, files: ['page.js']
            });
            running = {id: job.id, tabId, documentId: execution.documentId};
            if (job.action === 'text') {
              observed = await snapshot(tabId, running.documentId, target);
            }
          }
          result = await invoke(tabId, running.documentId, target, job);
        }
        if (result) {
          if (result.ok && job.action === 'dial') {
            callTab = tabId;
            result.tabId = tabId;
          } else if (result.ok && job.action === 'hangup') {
            callTab = undefined;
          }
          result = {...result, id: job.id};
          if (!result.partial) last = result;
        }
      }
      if (!job && tabId !== undefined && Date.now() >= scanAt) {
        scanAt = Date.now() + 1000;
        const [execution] = await chrome.scripting.executeScript({target: {tabId}, files: ['page.js']});
        observed = await snapshot(tabId, execution.documentId, target);
      }
    } catch {
      if (job) result = {id: job.id, ok: false, message: chrome.i18n.getMessage('tab_unavailable')};
    }
    if (connection !== port) return;
    const available = tabId !== undefined;
    if (ready !== available) {
      ready = available;
      chrome.action.setBadgeText({text: available ? 'ON' : chrome.i18n.getMessage('badge_idle')});
    }
    port.postMessage({available, result, observed});
  }

  port.onMessage.addListener(message => {
    if (typeof message.target === 'string') {
      target = message.target;
      poll(null);
    } else if (target) {
      timer = setTimeout(() => poll(message.job), message.job ? 225 : 500);
    }
  });
}

chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
chrome.action.onClicked.addListener(connect);
connect();
