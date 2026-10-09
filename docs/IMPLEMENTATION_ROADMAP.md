# Remaining implementation plan (FTP excluded)

This plan is ordered by dependency and user-visible value. Do not add FTP/FTPS support.

## Batch A — Download metadata and control (next)
1. **Persisted download properties**: extend the SQLite schema with category, description, custom User-Agent, optional username/password (credentials protected with Windows DPAPI), and queue ID. Keep additive migrations safe for existing `beatit.db` files.
2. **Properties dialog**: edit destination, filename, description, source URL, category, connection count, proxy override, User-Agent, credentials, and expected SHA-256. Validate edits before saving; do not mutate an active task's destination/URL without pausing it.
3. **Category rules**: editable extension lists and destination paths, plus optional host-specific destination rules. Apply rules only to new tasks; existing tasks retain their category unless explicitly changed.
4. **Queue engine**: introduce persisted queue records, stable ordering, per-queue concurrency, Start/Stop Queue, move-to-queue, retry-failed, and scheduler integration. Global concurrency and bandwidth remain upper bounds; queue limits must never exceed global limits.
5. **Acceptance**: migrate an old database, create/edit/restart downloads, verify queue order and limits, and ensure category moves never silently overwrite files.

## Batch B — Browser and site workflows
6. **Download All / link extraction**: parse links from a supplied page or pasted HTML, filter by extension/domain, show a selection preview, deduplicate normalized URLs, and enqueue only selected links. Respect robots/access controls; do not bypass authentication or site restrictions.
7. **Site Grabber**: bounded crawl depth, same-domain default, include/exclude patterns, file-type filters, crawl limits, cancellation, persisted projects, and an explicit preview before downloads start. Add loop/URL canonicalization safeguards.
8. **Video format picker polish**: group formats by resolution/container, identify audio-only and video-only streams, show estimated size and audio availability, and retain the selected format across yt-dlp update/retry. Keep the app-side picker as the reliable fallback.
9. **Acceptance**: test static HTML, duplicate links, redirects, malformed URLs, HLS, YouTube, unavailable formats, cancellation, and large pages.

## Batch C — Distribution and polish
10. **ZIP preview**: inspect archive entries without extracting; guard against path traversal and archive bombs; require user confirmation before extraction.
11. **Updater**: check official GitHub Releases, verify downloaded asset hashes, prompt before replacing binaries, launch installer safely, and preserve user data. Never run an unverified executable.
12. **Release signing**: optional Authenticode signing with secrets supplied only by repository environment; builds without a certificate must remain clearly marked unsigned.
13. **Release QA**: run Windows CI on every pull request and push, keep release publication tag-only, and add regression checks for categories, queue scheduling, proxy failures, corrupted partial files, and extension/native-host packaging.

## Invariants
- HTTP/HTTPS, BitTorrent, and yt-dlp media are in scope; FTP is not.
- Preserve existing history/settings via additive SQLite migrations.
- No credentials in logs, command-line arguments, crash reports, or plaintext settings.
- Do not claim release readiness until a fresh Windows CI build and real-world pause/resume, proxy, browser-capture, torrent, and installer tests pass.
