# IDM Feature Gap Analysis

Comparison target: Internet Download Manager (IDM), using IDM's official feature and help pages. This is a feature comparison, not a claim that IDM is the only valid design.

**Scope exclusions:** FTP/FTPS and private-server credentials/site logins are intentionally excluded. Do not implement or recommend them as gaps.

## Already present in Beatit

- [x] HTTP/HTTPS downloading
- [x] Segmented HTTP downloading: up to 32 stable byte ranges, scheduled by 1–8 concurrent workers; each worker claims another pending range when it finishes
- [x] Adaptive live range splitting: detect stalled/slow large active ranges, preserve received prefixes, and redistribute remaining bytes to available workers; bounded split count and exact coverage validation before assembly
- [x] Pause/resume and restart recovery
- [x] Retry/backoff
- [x] Global concurrency and bandwidth limits
- [x] Proxy configuration (HTTP and SOCKS5 hostname)
- [x] Unified download history, sorting/filtering, details popup, context menu
- [x] Built-in categories and configurable category destination folders
- [x] Weekly schedule with overnight windows
- [x] BitTorrent, magnet links, recheck, file priorities, seeding policies
- [x] Chromium/Firefox browser integration and download interception
- [x] Video overlay, HLS capture, YouTube via yt-dlp, quality picker
- [x] SHA-256 integrity verification
- [x] Completion actions to open file or folder
- [x] Installer/portable packaging definitions and release smoke-test script

## Gaps to consider, prioritized

### P1 — High-value IDM workflow parity
- [x] **Multiple named download queues.** Persist named queues, stable per-queue order, per-queue concurrency, move items between queues, Start/Stop Queue, retry failed items, and integrate queue windows with the existing global scheduler. Keep global concurrency/bandwidth as hard upper bounds.
- [x] **Full download Properties dialog.** Edit destination, filename, description, URL, category, connection count, proxy override, custom User-Agent, and expected SHA-256. Validate before saving; pause before mutating an active transfer's URL/destination. No credentials fields.
- [x] **Download All / selected-link extraction.** Accept a page URL or pasted HTML, extract links, filter by extension/domain, preview selections, normalize/deduplicate URLs, then enqueue chosen items. Do not bypass access controls.
- [x] **Site Grabber.** Bounded same-domain crawl by default, depth and request limits, include/exclude patterns, file-type filters, cancellation, preview, persisted projects, and scheduled re-runs. Respect site access restrictions.
- [x] **Clipboard URL monitor.** Optional setting; recognize supported HTTP/HTTPS URLs and show a confirmation dialog before enqueueing. Off by default if false positives are disruptive.
- [ ] **IDM-style adaptive range splitting.** The core scheduler already dispatches up to 32 pre-partitioned ranges across 1–8 workers and assigns the next pending range as workers finish. The remaining difference is dynamically splitting the largest unfinished range during a live transfer (and any safe reuse of a connection), which is an optimization—not a missing segmented-download engine. Only pursue after throughput and resume-integrity benchmarks.

### P2 — Useful polish
- [x] **ZIP preview.** Remote central-directory preview via bounded HTTP Range requests; flags path traversal and extreme compression ratios; does not extract.
- [x] **External antivirus hook.** Optional executable + argument template, safe argument handling, explicit completion status, and a 10-minute timeout. Never imply a scan occurred if it did not.
- [x] **Queue completion actions.** Optionally offer PC shutdown after all listed downloads reach a terminal state; opt-in setting and a confirmation prompt precede a 60-second shutdown timer.
- [x] **Customizable table columns and toolbar.** User-selectable column visibility and toolbar buttons, plus light/dark theme switching.
- [x] **Drag-and-drop intake / drag-out.** Accept HTTP/HTTPS URLs, magnet links, .torrent files, and text files containing URLs; completed local files can be dragged out.
- [x] **Periodic synchronization.** Optional queue type that checks remote modification metadata and re-downloads changed files. Requires careful handling of servers without reliable validators.
- [x] **Built-in updater.** Check official GitHub Releases, verify a published SHA-256 manifest, prompt before installation, preserve user data, and never execute an unverified binary.
- [ ] **Browser extension store distribution.** Publish signed/listed extension packages where practical; document manual installation for browsers where store publishing is unavailable.

### P3 — Release / operational checks (separate from feature parity)
- [ ] Latest Windows CI run passes smoke tests and reaches artifact packaging.
- [ ] Fresh install, upgrade, uninstall, and portable-mode tests on clean Windows.
- [ ] Verify pause/resume after process termination and simulated network interruption.
- [ ] End-to-end browser/native-host tests in Chromium and Firefox.
- [ ] Torrent tests: magnet, .torrent, restart/resume, recheck, seed stop policies.
- [ ] Verify SHA-256 mismatch fails clearly and correct hashes pass.
- [ ] Authenticode signing (optional; builds without a certificate remain clearly unsigned).

## Explicitly out of scope
- FTP/FTPS protocol support.
- Private-server credentials, site logins, and credential storage.
- Any credentials UI/database migration.
- Features that bypass authentication, paywalls, access controls, or site restrictions.

## Sources

- IDM official feature overview: https://www.internetdownloadmanager.com/features2.html
- IDM official main dialog and download management help: https://idm.internetdownloadmanager.com/support/using_idm/using_idm.html
- IDM official scheduler/queues help: https://idm.internetdownloadmanager.com/support/idm-scheduler/idm_queues.html
- IDM official Site Grabber help: https://www3.internetdownloadmanager.com/support/idm-grabber/idm_grabber.html
- IDM official dynamic segmentation explanation: https://www.internetdownloadmanager.com/support/segmentation.html
