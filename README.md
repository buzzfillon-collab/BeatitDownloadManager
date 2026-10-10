# Beatit Download Manager

Free and open-source Windows x64 download manager.

## Status

Beatit is a **feature-rich beta**. Core HTTP/HTTPS, BitTorrent, scheduling, categories, browser capture, video downloading, and persistence are implemented. The checklist below tracks implemented functionality separately from IDM-parity gaps and release validation.

## Implemented features

### Foundation and UI
- [x] Qt desktop application and CMake/vcpkg build
- [x] Windows x64 GitHub Actions build workflow
- [x] System tray icon and close-to-tray behavior
- [x] Unified HTTP + BitTorrent history
- [x] Sorting/filtering and download status/progress/speed display
- [x] Right-click actions: resume, pause, cancel, remove, details, open, checksum
- [x] Download details popup
- [x] Persistent SQLite download history and resume state

### HTTP/HTTPS
- [x] HTTP and HTTPS only; FTP/FTPS intentionally excluded
- [x] Segmented HTTP downloads: up to 32 stable byte ranges dispatched by configurable 1–8 concurrent workers
- [x] Workers claim the next pending range immediately after finishing their current range
- [x] Adaptive live splitting: when a large in-flight range stalls or falls materially behind, preserve its received prefix and requeue the remaining bytes for an available worker (bounded to 16 splits per download)
- [x] Persistent segment files and range requests for resume
- [x] Pause, resume, cancel, and up to five attempts for transient segment/network failures with exponential backoff; permanent HTTP errors fail immediately
- [x] Resume after application restart
- [x] Global concurrent-download limit
- [x] Global HTTP + BitTorrent bandwidth limit
- [x] HTTP/SOCKS5-hostname proxy settings
- [x] Optional SHA-256 expectation and post-download verification
- [x] Filename sanitization and download destination handling

### Download organization and scheduling
- [x] Automatic file-type categories: Video, Music, Documents, Programs, Other
- [x] Configurable destination folder for each built-in category
- [x] Download completion action: do nothing, open file, or open folder
- [x] Weekly scheduler/calendar with per-day start/end windows
- [x] Overnight scheduler windows
- [x] Basic persisted queue of downloads awaiting an available slot
- [x] Multiple user-defined queues with independent concurrency and start/stop controls
- [x] Move queued downloads between queues and retry failed items per queue
- [x] Editable extension rules for Video, Music, Documents, and Programs
- [x] Host-specific category rules and fully user-defined categories
- [x] Per-download Properties dialog (edit URL, destination, filename, category, description, connection count, proxy, User-Agent, checksum)

### BitTorrent
- [x] libtorrent session
- [x] Magnet links and .torrent files
- [x] DHT and trackers
- [x] Persistent torrent state and resume data
- [x] Piece verification/recheck
- [x] Incomplete-swarm availability warning with wait/continue choice
- [x] Stall detection and health reporting
- [x] Selective file priorities
- [x] Seeding policies: ratio, time, forever, stop immediately
- [x] Windows magnet protocol association in installer
- [x] Single-instance forwarding for external protocol launches

### Browser and media
- [x] Chromium Manifest V3 extension with popup and Firefox Manifest V2-compatible package
- [x] Firefox WebExtension/native-messaging path (separate package under `browser/extension/firefox/`)
- [x] Native host registration and app IPC bridge, including a live status handshake
- [x] Automatic interception of browser-created file downloads, with cancellation only after Beatit acknowledges the handoff
- [x] User-configurable intercepted extensions and master interception toggle
- [x] Context-menu actions for pages, links, selections, audio, and video
- [x] Extension popup with connection status, manual current-tab handoff, scan action, and detected-media list
- [x] Direct media detection from URL patterns, browser network requests, response MIME types, performance resource entries, and video/audio elements
- [x] HLS/M3U8 and DASH/MPD manifest discovery routed through yt-dlp when selected
- [x] YouTube and common yt-dlp-supported video-page detection and capture through yt-dlp
- [x] Video format/quality selection, concurrent media-fragment downloads, and retry/resume options
- [x] Bundled yt-dlp, FFmpeg, ffprobe, and Deno toolchain
- [x] yt-dlp nightly/stable channel selection and update/retry path
- [x] Extract and deduplicate HTTP/HTTPS links from a page; select individual links or Download All
- [x] Selected-text / pasted-text URL extraction, regex include/exclude filters, and extension filters
- [x] Site Grabber: same-host crawl with page/depth bounds, include/exclude and extension filters, cancellation, saved projects, and recurring background scans
- [x] Site Grabber include/exclude filters, saved projects, and scheduling
- [ ] Browser-extension store submission and signed distribution

#### Install the browser extension

1. Start Beatit once so it can register the native-messaging host in the current Windows user profile.
2. Open `chrome://extensions` (Chrome, Edge, Brave, Vivaldi, or Chromium), enable Developer mode, and choose **Load unpacked**.
3. Select the repository's `browser/extension/` folder. For the packaged ZIP, extract it first and load the folder containing the top-level Chromium `manifest.json`.
4. For Firefox, open `about:debugging#/runtime/this-firefox`, choose **Load Temporary Add-on**, and select `browser/extension/firefox/manifest.json` (or the same path inside the extracted ZIP).
5. Open the extension popup. It should show **Connected** when Beatit's native bridge is reachable.

Automatic interception is enabled by default for all browser download types (`*`). The popup lets you disable it, toggle media discovery, change the intercepted file extensions, paste a URL manually, scan the current page, and send detected media to Beatit. Detected streams are listed for an explicit user action; merely discovering an HLS/DASH manifest does not automatically start a media download.

#### Browser integration test checklist

- [ ] Download a normal `.zip` or `.exe` file: Beatit should receive it and the browser copy should be cancelled only after acknowledgment.
- [ ] Disable automatic interception in the popup and verify the browser keeps the download.
- [ ] Open a page with a direct MP4/WebM or audio URL and verify it appears in the detected-media list.
- [ ] Open an HLS `.m3u8` or DASH `.mpd` player and verify the manifest appears once, without flooding the list with media fragments.
- [ ] Open a supported video-site page and use the popup or in-player Beatit button to hand it to yt-dlp.
- [ ] Test the same flows in Chromium and Firefox separately; Windows CI validates package structure and JavaScript syntax, not live browser behavior.

### IDM-parity features not yet implemented
- [ ] IDM-style adaptive splitting of the largest remaining range during an active transfer (existing 32-range worker scheduling is already implemented; still an outstanding throughput optimization)
- [x] Optional clipboard URL monitoring with confirmation before enqueueing
- [x] Drag-and-drop URL, magnet, torrent, URL-list intake, and completed-file drag-out
- [x] Remote ZIP central-directory preview via bounded HTTP Range requests, with path-traversal and extreme compression-ratio warnings
- [x] Configurable external antivirus process on completion, with explicit process status
- [x] Opt-in shutdown offer after the download queue completes
- [x] Built-in GitHub Releases updater with SHA-256 verification and installer handoff
- [x] Customizable column visibility, toolbar buttons, and light/dark theme
- [x] Periodic synchronization jobs using HTTP ETag / Last-Modified / size validators

### Packaging and release
- [x] Inno Setup per-user installer definition
- [x] Portable ZIP packaging workflow definition
- [x] Browser extension ZIP packaging workflow definition
- [x] Release smoke-test script
- [ ] Latest clean Windows CI run completing all smoke tests and packaging steps
- [ ] Fresh installer and portable ZIP manually verified on a clean Windows environment
- [ ] SHA-256 checksums generated and verified for a successful release build
- [ ] Authenticode-signed binaries
- [ ] Stable public release

## Known exclusions
- FTP/FTPS support: intentionally excluded.
- Private-server credentials / saved site logins: intentionally excluded.
- No credential fields should be added to the database, UI, logs, or settings.

## Architecture

Qt 6 desktop UI -> DownloadManager -> HTTP engine (libcurl) / BitTorrent engine (libtorrent) -> SQLite persistence.

Browser path: WebExtension -> native messaging -> BeatitBrowserHost -> local Qt IPC -> Beatit -> HTTP/yt-dlp.

## IDM comparison

See [IDM feature gap analysis](docs/IDM_FEATURE_GAP_ANALYSIS.md) for a feature-by-feature comparison, with intentionally excluded features kept out of scope. See [QDM implementation notes](docs/QDM_IMPLEMENTATION_NOTES.md) for the feature comparison, upstream inspiration, and verification checklist.

## License

GNU General Public License v3.0.
