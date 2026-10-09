(() => {
  if (window.top !== window) return;
  const seen = new WeakSet();
  const knownUrls = new Set();
  let scanTimer = 0;

  function sendDetected(url, kind) {
    if (!url || !/^https?:\/\//i.test(url) || knownUrls.has(url)) return;
    knownUrls.add(url);
    if (knownUrls.size > 250) knownUrls.clear();
    chrome.runtime.sendMessage({
      action: "media-detected",
      url,
      pageUrl: location.href,
      title: document.title,
      kind
    }).catch(() => {});
  }
  function classifyUrl(raw) {
    try {
      const url = new URL(raw, location.href);
      if (!/^https?:$/.test(url.protocol)) return "";
      if (/\.m3u8(?:$|[?#])/i.test(url.href)) return "hls";
      if (/\.mpd(?:$|[?#])/i.test(url.href)) return "dash";
      if (/\.(mp4|webm|mkv|mov|m4v|avi|mp3|m4a|aac|ogg|opus|flac|wav)(?:$|[?#])/i.test(url.href)) return "media";
    } catch (_) {}
    return "";
  }
  function scanResources() {
    try {
      for (const entry of performance.getEntriesByType("resource")) {
        const kind = classifyUrl(entry.name);
        if (kind === "hls" || kind === "dash" || kind === "media") sendDetected(entry.name, kind);
      }
    } catch (_) {}
    document.querySelectorAll("video,audio").forEach(media => {
      const candidates = [media.currentSrc, media.src, ...Array.from(media.querySelectorAll("source"), node => node.src)];
      for (const raw of candidates) {
        const kind = classifyUrl(raw);
        if (kind) sendDetected(raw, kind);
      }
    });
  }
  function attach(video) {
    if (seen.has(video)) return;
    seen.add(video);
    const parent = video.parentElement;
    if (!parent) return;
    const host = document.createElement("div");
    host.style.cssText = "position:absolute;z-index:2147483647;display:none;font:12px Arial,sans-serif;pointer-events:auto;";
    const shadow = host.attachShadow({mode:"closed"});
    const wrap = document.createElement("div");
    wrap.innerHTML = '<button data-mode="video" title="Download this video">↓ Beatit</button><button data-mode="mp3" title="Extract audio with Beatit">♫ MP3</button>';
    const style = document.createElement("style");
    style.textContent = "div{display:flex;gap:5px}button{border:0;border-radius:5px;padding:6px 9px;background:#0b2334;color:#e5fbff;cursor:pointer;box-shadow:0 2px 8px #0008;font-weight:600;border:1px solid #12b8c5}button:hover{background:#07576b}";
    shadow.append(style, wrap);
    if (getComputedStyle(parent).position === "static") parent.style.position = "relative";
    parent.appendChild(host);
    function position() {
      const rect = video.getBoundingClientRect();
      const p = parent.getBoundingClientRect();
      host.style.left = Math.max(4, rect.right - p.left - 165) + "px";
      host.style.top = Math.max(4, rect.top - p.top + 8) + "px";
      host.style.display = rect.width > 180 && rect.height > 100 ? "block" : "none";
    }
    const observer = new ResizeObserver(position);
    observer.observe(video);
    window.addEventListener("scroll", position, {passive:true});
    video.addEventListener("loadedmetadata", () => { position(); scanResources(); });
    position();
    wrap.addEventListener("click", async event => {
      const button = event.target.closest("button");
      if (!button) return;
      event.preventDefault();
      event.stopPropagation();
      const mode = button.dataset.mode;
      const current = video.currentSrc || video.src || "";
      const mediaUrl = /^https?:/i.test(current) ? current : "";
      button.disabled = true;
      button.textContent = mode === "mp3" ? "♫ Sending…" : "↓ Sending…";
      try {
        const response = await chrome.runtime.sendMessage({
          action:"download-media", mode, pageUrl:location.href, mediaUrl, title:document.title,
          kind:mode === "mp3" ? "audio" : (classifyUrl(mediaUrl) || "video")
        });
        button.textContent = response?.ok ? "✓ Sent" : "× Retry";
      } catch (_) { button.textContent = "× Retry"; }
      setTimeout(() => {
        button.disabled = false;
        button.textContent = mode === "mp3" ? "♫ MP3" : "↓ Beatit";
      }, 1800);
    });
  }
  function scan() {
    document.querySelectorAll("video").forEach(attach);
    scanResources();
    // Some players expose only blob: URLs. In that case the page URL is the useful yt-dlp target.
    const videos = Array.from(document.querySelectorAll("video"));
    const audios = Array.from(document.querySelectorAll("audio"));
    if (videos.some(media => !classifyUrl(media.currentSrc || media.src)))
      sendDetected(location.href, "video-page");
    else if (audios.some(media => !classifyUrl(media.currentSrc || media.src)))
      sendDetected(location.href, "audio");
  }
  chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
    if (message?.action !== "scan-media") return;
    scanResources();
    sendResponse({ ok: true });
  });
  scan();
  const mutationObserver = new MutationObserver(() => {
    clearTimeout(scanTimer);
    scanTimer = setTimeout(scan, 250);
  });
  if (document.documentElement) mutationObserver.observe(document.documentElement, {childList:true,subtree:true,attributes:true,attributeFilter:["src"]});
  setInterval(scanResources, 3000);
  window.addEventListener("load", scanResources, {once:true});
})();