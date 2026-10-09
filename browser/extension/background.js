const HOST = "com.beatit.download_manager";
function send(payload) {
  return chrome.runtime.sendNativeMessage(HOST, payload).catch(() => null);
}
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || message.action !== "download-media" || !message.pageUrl) return;
  send({
    url: message.mediaUrl || message.pageUrl,
    pageUrl: message.pageUrl,
    title: message.title || sender.tab?.title || "",
    kind: message.mode === "mp3" ? "audio" : "video"
  }).then(sendResponse);
  return true;
});
function classify(url) {
  const u = new URL(url);
  const lower = u.pathname.toLowerCase();
  if (lower.includes(".m3u8")) return "hls";
  if (lower.endsWith(".mpd")) return "dash";
  if (lower.endsWith(".ts")) return "ts";
  if (/\.(mp4|webm|mkv|mov|m4v|mp3|aac|flac)$/i.test(lower)) return "media";
  if (/^(www\.)?(youtube\.com|youtube-nocookie\.com|youtu\.be)$/i.test(u.hostname)) return "youtube";
  return "page";
}
chrome.action.onClicked.addListener(async (tab) => {
  if (!tab?.url || !/^https?:/i.test(tab.url)) return;
  send({url: tab.url, title: tab.title || "", kind: classify(tab.url)});
});
chrome.webRequest.onBeforeRequest.addListener(
  (details) => {
    if (details.tabId < 0 || !/^https?:/i.test(details.url)) return;
    const kind = classify(details.url);
    if (kind === "hls" || kind === "dash")
      send({url: details.url, title: "", kind});
  },
  {urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "other"]}
);

chrome.downloads.onCreated.addListener(async (item) => {
  if (!item?.url || !/^https?:/i.test(item.url) || item.state !== "in_progress") return;
  const response = await send({
    url: item.url,
    title: item.filename ? item.filename.split(/[\\/]/).pop() : "",
    kind: "download"
  });
  if (response?.ok === true) {
    try { await chrome.downloads.cancel(item.id); } catch (_) {}
  }
});


chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "beatit-download-page",
    title: "Download with Beatit",
    contexts: ["page", "video", "link"]
  });
});
chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (!tab?.url) return;
  const url = info.linkUrl || info.srcUrl || tab.url;
  if (!/^https?:/i.test(url)) return;
  send({
    url,
    pageUrl: tab.url,
    title: tab.title || "",
    kind: info.mediaType === "video" ? "video" : "download"
  });
});
