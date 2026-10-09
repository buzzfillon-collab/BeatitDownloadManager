const $ = id => document.getElementById(id);
let activeTab = null;
const defaults = {
  interceptEnabled: true,
  mediaDetectionEnabled: true,
  interceptExtensions: "7z, apk, avi, bin, bz2, csv, deb, dmg, doc, docx, exe, flac, flv, gz, iso, m4a, m4v, mkv, mov, mp3, mp4, msi, odt, ogg, pdf, pkg, ppt, pptx, rar, rpm, tar, torrent, txt, wav, webm, wma, wmv, xls, xlsx, xz, zip",
  detectedMedia: []
};
function send(message) {
  return chrome.runtime.sendMessage(message).catch(error => ({ok:false,error:String(error)}));
}
function kindLabel(kind) {
  return ({hls:"HLS stream",dash:"DASH stream",media:"Direct media",youtube:"YouTube", "video-page":"Video page",download:"File download",audio:"Audio"})[kind] || kind || "Media";
}
function displayUrl(url) {
  try { const u = new URL(url); return u.hostname + u.pathname.slice(0,70); } catch (_) { return url; }
}
function renderMedia(items) {
  const list = $("mediaList");
  $("mediaCount").textContent = String(items.length);
  list.replaceChildren();
  if (!items.length) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.textContent = "No media detected yet. Start playback or open a media page.";
    list.append(empty);
    return;
  }
  for (const item of items.slice(0,20)) {
    const card = document.createElement("div"); card.className = "media-item";
    const title = document.createElement("div"); title.className = "media-title"; title.textContent = item.title || displayUrl(item.url);
    const meta = document.createElement("div"); meta.className = "media-meta"; meta.textContent = kindLabel(item.kind) + " · " + displayUrl(item.url);
    const actions = document.createElement("div"); actions.className = "media-actions";
    const download = document.createElement("button"); download.className = "primary"; download.textContent = "Send to Beatit";
    download.addEventListener("click", async () => {
      download.disabled = true; download.textContent = "Sending…";
      const response = await send({action:"download-url",url:item.url,pageUrl:item.pageUrl||"",title:item.title||"",kind:item.kind||"download"});
      download.textContent = response?.ok ? "Sent ✓" : "Failed — retry";
      download.disabled = false;
    });
    const open = document.createElement("button"); open.textContent = "Open page";
    open.addEventListener("click", () => chrome.tabs.create({url:item.pageUrl || item.url}));
    actions.append(download,open); card.append(title,meta,actions); list.append(card);
  }
}
async function refreshMedia() {
  const data = await chrome.storage.local.get({detectedMedia:[]});
  renderMedia(Array.isArray(data.detectedMedia) ? data.detectedMedia : []);
}
async function load() {
  const [stored] = await Promise.all([chrome.storage.local.get(defaults)]);
  $("intercept").checked = stored.interceptEnabled !== false;
  $("mediaDetection").checked = stored.mediaDetectionEnabled !== false;
  $("extensions").value = stored.interceptExtensions || defaults.interceptExtensions;
  const tabs = await chrome.tabs.query({active:true,currentWindow:true});
  activeTab = tabs[0] || null;
  $("tabTitle").textContent = activeTab?.title || activeTab?.url || "No active tab";
  const status = await send({action:"get-status"});
  $("connection").textContent = status?.ok ? "Connected" : "App offline";
  $("connection").className = "status " + (status?.ok ? "ok" : "bad");
  await refreshMedia();
}
$("save").addEventListener("click", async () => {
  const response = await send({
    action:"save-settings",
    interceptEnabled:$("intercept").checked,
    mediaDetectionEnabled:$("mediaDetection").checked,
    interceptExtensions:$("extensions").value
  });
  $("save").textContent = response?.ok ? "Saved ✓" : "Save failed";
  setTimeout(() => $("save").textContent = "Save settings", 1200);
});
$("downloadPage").addEventListener("click", async () => {
  if (!activeTab?.url || !/^https?:/i.test(activeTab.url)) return;
  const response = await send({action:"download-url",url:activeTab.url,pageUrl:activeTab.url,title:activeTab.title||"",kind:/youtube\.com|youtu\.be|vimeo\.com|twitch\.tv|dailymotion\.com|facebook\.com|instagram\.com|tiktok\.com|streamable\.com|twitter\.com|x\.com/i.test(activeTab.url)?"youtube":"download"});
  $("downloadPage").textContent = response?.ok ? "Sent ✓" : "Failed — retry";
  setTimeout(() => $("downloadPage").textContent = "Download page", 1300);
});
$("captureMedia").addEventListener("click", async () => {
  if (!activeTab?.id) return;
  try { await chrome.tabs.sendMessage(activeTab.id,{action:"scan-media"}); } catch (_) {}
  await refreshMedia();
  $("captureMedia").textContent = "Scanned";
  setTimeout(() => $("captureMedia").textContent = "Scan for media", 1000);
});
$("clear").addEventListener("click", async () => { await send({action:"clear-media"}); await refreshMedia(); });
chrome.storage.onChanged.addListener((changes, area) => { if (area === "local" && changes.detectedMedia) refreshMedia(); });
load();