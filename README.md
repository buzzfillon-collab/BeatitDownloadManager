# Beatit Download Manager

Free and open-source Windows x64 download manager.

## Planned features
- Segmented HTTP/HTTPS downloads
- Pause/resume and persistent state
- Queues, scheduling and speed limits
- Browser integration
- BitTorrent via libtorrent
- Prebuilt Windows x64 EXE and portable ZIP releases

## Architecture
Qt 6 desktop UI -> DownloadManager -> HTTP engine (libcurl) / BitTorrent engine (libtorrent) -> persistence.

## Development roadmap
### Phase 0 — Foundation
- [x] Qt desktop shell
- [x] Download task abstraction
- [x] Download manager abstraction
- [x] CMake project
- [x] vcpkg manifest
- [ ] Windows CI

### Phase 1 — HTTP engine
- [ ] HEAD/metadata probing
- [ ] Range capability detection
- [ ] Segmented transfers
- [ ] Persistent partial files
- [ ] Pause/resume
- [ ] Retry/backoff
- [ ] Progress/speed reporting
- [ ] Filename and collision handling
- [ ] Checksums

### Phase 2 — Manager
- [ ] SQLite persistence
- [ ] Queue/scheduler
- [ ] Limits
- [ ] History

### Phase 3 — BitTorrent
- [ ] libtorrent session
- [ ] Magnet links
- [ ] Torrent files
- [ ] DHT/trackers
- [ ] Piece verification
- [ ] Seeding controls

### Phase 4 — Browser integration
- [ ] Chromium native messaging
- [ ] Firefox native messaging
- [ ] Browser extensions

### Phase 5 — Release
- [ ] Installer
- [ ] Portable ZIP
- [ ] Automated Windows x64 builds
- [ ] Release smoke tests

## License
GNU General Public License v3.0.
