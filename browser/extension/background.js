const HOST = "com.beatit.download_manager";
function send(payload) { chrome.runtime.sendNativeMessage(HOST, payload).catch(() => {}); }
function classify(url) {
  const u = new URL(url);
  const lower = u.pathname.toLowerCase();
  if (lower.includes(".m3u8")) return "hls";
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
    if (kind === "hls")
      send({url: details.url, title: "", kind});
  },
  {urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "other"]}
);