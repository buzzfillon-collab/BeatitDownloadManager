const HOST = "com.beatit.download_manager";
const MEDIA_LIMIT = 30;
const DEFAULT_EXTENSIONS = ["*"];
const recent = new Map();

function send(payload) {
  return chrome.runtime.sendNativeMessage(HOST, payload).catch(error => ({
    ok: false,
    error: String(error || "native-messaging-failed")
  }));
}
async function settings() {
  const value = await chrome.storage.local.get({
    interceptEnabled: true,
    interceptExtensions: DEFAULT_EXTENSIONS.join(","),
    mediaDetectionEnabled: true
  });
  return value;
}
function isHttp(url) {
  return typeof url === "string" && /^https?:\/\//i.test(url);
}
function classify(rawUrl, mime = "", resourceType = "") {
  let u;
  try { u = new URL(rawUrl); } catch (_) { return "page"; }
  if (!/^https?:$/.test(u.protocol)) return "page";
  const path = u.pathname.toLowerCase();
  const host = u.hostname.toLowerCase().replace(/^www\./, "");
  if (/\.m3u8$/i.test(path) || /(?:format|type)=hls(?:&|$)/i.test(u.search)) return "hls";
  if (/\.mpd$/i.test(path)) return "dash";
  if (/(^|\.)youtube\.com$|(^|\.)youtube-nocookie\.com$|^youtu\.be$/.test(host)) return "youtube";
  if (/(^|\.)youtu\.be$|(^|\.)vimeo\.com$|(^|\.)twitch\.tv$|(^|\.)dailymotion\.com$|(^|\.)facebook\.com$|(^|\.)instagram\.com$|(^|\.)tiktok\.com$/.test(host)) return "video-page";
  if (/\.(mp4|webm|mkv|mov|m4v|avi|mp3|m4a|aac|ogg|opus|flac|wav|wma|flv|m4s)$/i.test(path)) {
    if (/\.m4s$/i.test(path)) return "segment";
    return "media";
  }
  const type = String(mime).split(";")[0].trim().toLowerCase();
  if (type === "application/vnd.apple.mpegurl" || type === "application/x-mpegurl") return "hls";
  if (type === "application/dash+xml") return "dash";
  if (/^(video|audio)\//.test(type)) return "media";
  return "page";
}
function fileExtension(url, filename = "") {
  const candidate = filename || (() => {
    try { return new URL(url).pathname.split("/").pop() || ""; } catch (_) { return ""; }
  })();
  const match = candidate.toLowerCase().match(/\.([a-z0-9]{1,12})$/);
  return match ? match[1] : "";
}
function isAllowedFile(url, filename, extensionSetting) {
  const configured = String(extensionSetting || DEFAULT_EXTENSIONS.join(","))
    .split(/[,\s;]+/).map(x => x.trim().toLowerCase().replace(/^\./, "")).filter(Boolean);
  if (configured.includes("*") || configured.includes("all")) return true;
  const ext = fileExtension(url, filename);
  return !!ext && configured.includes(ext);
}
async function rememberMedia(item) {
  const key = item.url;
  const now = Date.now();
  if (!key || (recent.has(key) && now - recent.get(key) < 15000)) return false;
  recent.set(key, now);
  if (recent.size > 200) {
    for (const [url, time] of recent) if (now - time > 60000) recent.delete(url);
  }
  const data = await chrome.storage.local.get({ detectedMedia: [] });
  const list = Array.isArray(data.detectedMedia) ? data.detectedMedia : [];
  const next = [{
    ...item,
    detectedAt: now
  }, ...list.filter(entry => entry.url !== key)].slice(0, MEDIA_LIMIT);
  await chrome.storage.local.set({ detectedMedia: next });
  return true;
}
async function capture(url, options = {}) {
  if (!isHttp(url)) return { ok: false, error: "invalid-url" };
  const payload = {
    url,
    pageUrl: options.pageUrl || "",
    title: options.title || "",
    kind: options.kind || "download"
  };
  if (options.userAgent) payload.userAgent = options.userAgent;
  if (options.referer) payload.referer = options.referer;
  if (options.cookies) payload.cookies = options.cookies;
  return send(payload);
}
async function tabInfo(tabId) {
  if (typeof tabId !== "number" || tabId < 0) return {};
  try {
    const tab = await chrome.tabs.get(tabId);
    return { pageUrl: tab.url || "", title: tab.title || "" };
  } catch (_) { return {}; }
}
async function detectRequest(details, mime = "") {
  const config = await settings();
  if (!config.mediaDetectionEnabled || details.tabId < 0 || !isHttp(details.url)) return;
  const kind = classify(details.url, mime, details.type);
  if (!["hls", "dash", "media"].includes(kind)) return;
  // Do not send individual transport-stream or fragmented-media chunks to the app.
  if (kind === "media" && /\.(ts|m4s|cmfv|cmfa)$/i.test(new URL(details.url).pathname)) return;
  const tab = await tabInfo(details.tabId);
  const title = tab.title || "";
  await rememberMedia({ url: details.url, pageUrl: tab.pageUrl, title, kind });
}
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message !== "object") return;
  if (message.action === "get-status") {
    send({ action: "status" }).then(sendResponse);
    return true;
  }
  if (message.action === "media-detected") {
    const url = message.url;
    if (!isHttp(url)) { sendResponse({ ok: false, error: "invalid-url" }); return; }
    rememberMedia({
      url,
      pageUrl: message.pageUrl || sender.tab?.url || "",
      title: message.title || sender.tab?.title || "",
      kind: message.kind || classify(url)
    }).then(() => sendResponse({ ok: true }));
    return true;
  }
  if (message.action === "clear-media") {
    chrome.storage.local.set({ detectedMedia: [] }).then(() => sendResponse({ ok: true }));
    return true;
  }
  if (message.action === "save-settings") {
    chrome.storage.local.set({
      interceptEnabled: message.interceptEnabled !== false,
      mediaDetectionEnabled: message.mediaDetectionEnabled !== false,
      interceptExtensions: String(message.interceptExtensions || DEFAULT_EXTENSIONS.join(","))
    }).then(() => sendResponse({ ok: true }));
    return true;
  }
  if (message.action === "download-media") {
    const pageUrl = message.pageUrl || sender.tab?.url || "";
    capture(message.mediaUrl || pageUrl, {
      pageUrl,
      title: message.title || sender.tab?.title || "",
      kind: message.kind || (message.mode === "mp3" ? "audio" : "video")
    }).then(sendResponse);
    return true;
  }
  if (message.action === "download-url") {
    capture(message.url, {
      pageUrl: message.pageUrl || sender.tab?.url || "",
      title: message.title || sender.tab?.title || "",
      kind: message.kind || classify(message.url)
    }).then(sendResponse);
    return true;
  }
});
const actionApi = chrome.action || chrome.browserAction;
if (actionApi?.onClicked) {
  actionApi.onClicked.addListener(async tab => {
    if (!tab?.url || !isHttp(tab.url)) return;
    await capture(tab.url, { title: tab.title || "", pageUrl: tab.url, kind: classify(tab.url) });
  });
}
chrome.webRequest.onBeforeRequest.addListener(details => {
  void detectRequest(details);
}, { urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "other"] });
chrome.webRequest.onHeadersReceived.addListener(details => {
  const header = (details.responseHeaders || []).find(h => h.name.toLowerCase() === "content-type");
  if (header) void detectRequest(details, String(header.value || ""));
}, { urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "other"] }, ["responseHeaders"]);
chrome.downloads.onCreated.addListener(async item => {
  if (!item?.url || !isHttp(item.url) || item.byExtensionId === chrome.runtime.id) return;
  const config = await settings();
  if (!config.interceptEnabled || !isAllowedFile(item.url, item.filename, config.interceptExtensions)) return;
  const tab = await tabInfo(item.tabId);
  const response = await capture(item.url, {
    pageUrl: tab.pageUrl || "",
    title: item.filename ? item.filename.split(/[\\/]/).pop() : (tab.title || ""),
    kind: "download"
  });
  // Cancel the browser's copy only after Beatit has acknowledged the handoff.
  if (response?.ok === true) {
    try { await chrome.downloads.cancel(item.id); } catch (_) {}
  }
});
chrome.runtime.onInstalled.addListener(() => {
  const menus = [
    { id: "beatit-download-page", title: "Download page with Beatit", contexts: ["page"] },
    { id: "beatit-download-link", title: "Download link with Beatit", contexts: ["link"] },
    { id: "beatit-download-media", title: "Download media with Beatit", contexts: ["video", "audio"] },
    { id: "beatit-download-selection", title: "Download selected URL with Beatit", contexts: ["selection"] }
  ];
  for (const menu of menus) chrome.contextMenus.create(menu, () => void chrome.runtime.lastError);
});
chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  const url = info.linkUrl || info.srcUrl || (info.selectionText || "").trim() || tab?.url;
  if (!isHttp(url)) return;
  const kind = info.mediaType === "audio" ? "audio" :
    info.mediaType === "video" ? "video" : classify(url);
  await capture(url, { pageUrl: tab?.url || "", title: tab?.title || "", kind });
});
chrome.tabs.onUpdated.addListener((tabId, changeInfo, tab) => {
  if (changeInfo.status !== "complete" || !tab?.url || !isHttp(tab.url)) return;
  const kind = classify(tab.url);
  if (kind === "youtube" || kind === "video-page") {
    void rememberMedia({ url: tab.url, pageUrl: tab.url, title: tab.title || "", kind });
  }
});
