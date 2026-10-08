(() => {
  if (window.top !== window) return;
  const seen = new WeakSet();

  function attach(video) {
    if (seen.has(video)) return;
    seen.add(video);
    if (!video.parentElement) return;

    const host = document.createElement("div");
    host.style.cssText = "position:absolute;z-index:2147483647;display:none;font:12px Arial,sans-serif;pointer-events:auto;";
    const shadow = host.attachShadow({mode:"closed"});
    const wrap = document.createElement("div");
    wrap.innerHTML = '<button data-mode="video" title="Download this video">↓ Download</button><button data-mode="mp3" title="Download audio as MP3">♫ MP3</button>';
    const style = document.createElement("style");
    style.textContent = `
      div { display:flex; gap:5px; }
      button { border:0; border-radius:5px; padding:6px 9px; background:#17191d; color:#fff; cursor:pointer; box-shadow:0 2px 8px #0008; font-weight:600; }
      button:hover { background:#3a3f48; }
    `;
    shadow.append(style, wrap);

    const parent = video.parentElement;
    if (getComputedStyle(parent).position === "static") parent.style.position = "relative";
    parent.appendChild(host);

    function position() {
      const rect = video.getBoundingClientRect();
      const p = parent.getBoundingClientRect();
      host.style.left = Math.max(4, rect.right - p.left - 190) + "px";
      host.style.top = Math.max(4, rect.top - p.top + 8) + "px";
      host.style.display = rect.width > 180 && rect.height > 100 ? "block" : "none";
    }
    const observer = new ResizeObserver(position);
    observer.observe(video);
    window.addEventListener("scroll", position, {passive:true});
    video.addEventListener("loadedmetadata", position);
    position();

    wrap.addEventListener("click", async event => {
      const button = event.target.closest("button");
      if (!button) return;
      event.preventDefault();
      event.stopPropagation();
      const mode = button.dataset.mode;
      const pageUrl = location.href;
      const current = video.currentSrc || video.src || "";
      const mediaUrl = /^https?:/i.test(current) ? current : "";
      button.disabled = true;
      button.textContent = mode === "mp3" ? "♫ MP3…" : "↓ Sending…";
      try {
        const response = await chrome.runtime.sendMessage({
          action:"download-media", mode, pageUrl, mediaUrl, title:document.title
        });
        button.textContent = response?.ok ? "✓ Sent" : "× Retry";
      } catch (_) {
        button.textContent = "× Retry";
      }
      setTimeout(() => {
        button.disabled = false;
        button.textContent = mode === "mp3" ? "♫ MP3" : "↓ Download";
      }, 1800);
    });
  }

  function scan() { document.querySelectorAll("video").forEach(attach); }
  scan();
  new MutationObserver(scan).observe(document.documentElement, {childList:true, subtree:true});
})();
