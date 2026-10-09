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
- [x] Persistent segment files and range requests for resume
- [x] Pause, resume, cancel, and retry/backoff
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
- [ ] Host-specific category rules and fully user-defined categories
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
- [x] Chromium Manifest V3 extension
- [x] Firefox WebExtension/native-messaging path
- [x] Native host registration and app IPC bridge
- [x] Browser download interception
- [x] Context-menu download action
- [x] Direct media capture and video-page overlay
- [x] HLS/M3U8 capture
- [x] YouTube capture through yt-dlp
- [x] Video format/quality selection
- [x] Bundled yt-dlp, FFmpeg, ffprobe, and Deno toolchain
- [x] yt-dlp nightly/stable channel selection and update/retry path
- [x] Extract and deduplicate HTTP/HTTPS links from a page; select individual links or Download All
- [ ] Selected-text link extraction and advanced link-type filters
- [x] Basic Site Grabber: bounded same-host crawl with a configurable page limit and link preview
- [ ] Site Grabber include/exclude filters, saved projects, and scheduling
- [ ] Browser-extension store submission and signed distribution

### IDM-parity features not yet implemented
- [ ] IDM-style adaptive splitting of the largest remaining range during an active transfer (existing 32-range worker scheduling is already implemented)
- [ ] Clipboard URL monitoring with a confirmation prompt
- [ ] Drag-and-drop URL/file intake and drag-out support
- [ ] ZIP archive preview before downloading/extracting
- [ ] Configurable external antivirus scan on completion
- [ ] Optional computer shutdown after the download queue completes
- [ ] Built-in release updater with hash verification and safe installer handoff
- [ ] Customizable columns, toolbar, and theme/skin system
- [ ] Periodic synchronization queues for re-downloading changed files

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

See [IDM feature gap analysis](docs/IDM_FEATURE_GAP_ANALYSIS.md) for a feature-by-feature comparison, with intentionally excluded features kept out of scope.

## License

GNU General Public License v3.0.
