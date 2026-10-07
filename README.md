# Beatit Download Manager

Free and open-source Windows x64 download manager.

## Current status

Beatit is in the **beta / integration-hardening** stage. The core HTTP and BitTorrent download paths are implemented, persistent state is in place, browser integration is wired end-to-end, and the Windows portable + installer packaging pipeline is operational. The project remains beta until release smoke testing, signing, and final real-world validation are complete.

## Completed

### Foundation
- [x] Qt desktop shell
- [x] CMake project
- [x] vcpkg dependency manifest
- [x] Windows x64 GitHub Actions build
- [x] Portable ZIP artifact
- [x] Persistent system tray / close-to-tray behavior

### HTTP/HTTPS
- [x] Multi-connection segmented downloads (1-8 connections)
- [x] Range/segmentation with persistent .part.N files
- [x] Pause/resume
- [x] Retry/backoff
- [x] Aggregate progress and speed reporting
- [x] Persistent download history
- [x] Resume after application restart
- [x] Cancellation and cleanup
- [x] Filename/history UI
- [ ] Checksums / post-download integrity verification

### Download manager/UI
- [x] Queueing and persistence
- [x] Unified HTTP + BitTorrent history
- [x] Sorting/filtering
- [x] Pause/resume controls
- [x] Right-click download context menu
- [x] Remove from history with optional source-file deletion
- [x] Torrent recheck control
- [ ] Full scheduler/calendar UI
- [ ] Bandwidth limiting

### BitTorrent
- [x] libtorrent session
- [x] Magnet links
- [x] .torrent files
- [x] DHT / trackers
- [x] Piece verification / recheck
- [x] Persistent torrent state / resume data
- [x] Incomplete swarm availability warning and wait/continue choice
- [x] Stall detection and health reporting
- [x] Seeding ratio/time/forever/immediate policies
- [x] Selective file priorities
- [ ] Windows magnet: protocol association
- [ ] Single-instance command forwarding for external protocol launches

### Browser integration
- [x] Manifest V3 extension
- [x] Chromium native messaging
- [x] Firefox native messaging
- [x] Browser download interception
- [x] HLS M3U8 capture
- [x] YouTube capture via yt-dlp
- [x] Direct media capture
- [x] Per-user Windows native-host registration
- [x] Stable Chromium extension identity
- [x] Firefox extension identity
- [x] yt-dlp nightly/stable channel selection
- [x] Automatic yt-dlp update/retry path
- [x] Browser extension ZIP packaging
- [ ] Browser-extension store submission / signed distribution

### Release
- [x] Windows x64 automated build
- [x] Portable ZIP
- [x] Bundled browser extension
- [x] Bundled yt-dlp/FFmpeg/Deno toolchain
- [x] Windows installer (Inno Setup, per-user)
- [x] Automated release smoke tests
- [x] SHA-256 artifact checksums
- [ ] Authenticode-signed binaries
- [ ] Stable public release

## Architecture

Qt 6 desktop UI -> DownloadManager -> HTTP engine (libcurl) / BitTorrent engine (libtorrent) -> SQLite persistence.

Browser path: WebExtension -> native messaging -> BeatitBrowserHost -> local Qt IPC -> Beatit -> HTTP/yt-dlp.

## License

GNU General Public License v3.0.
