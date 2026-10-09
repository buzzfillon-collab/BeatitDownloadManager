# Remaining implementation plan (FTP and credentials excluded)

This is the working feature plan, ordered by user value and dependency. FTP/FTPS, private-server credentials, and saved site logins are intentionally out of scope. Existing functionality is not to be rebuilt merely because its implementation differs from IDM.

## Batch A — Queue control and download properties (next)
1. **Named queue model and controls**: promote the existing `queue_id` field from metadata into real behavior. Add persisted queue definitions, stable queue order, per-queue concurrency, Start/Stop Queue, move-to-queue, retry-failed, and scheduler integration. Global concurrency and bandwidth remain hard upper bounds.
2. **Queue migration and recovery**: preserve all existing downloads as members of the default `main` queue; old databases must migrate additively without losing history or resume state. Stopped-queue items must remain queued across restart.
3. **Properties dialog**: edit destination, filename, description, source URL, category, connection count, proxy override, User-Agent, and expected SHA-256. No credentials fields. Validate edits before saving; active transfers must be paused before changing URL or destination, and changing URL must invalidate incompatible partial segments.
4. **Category rules**: editable extension lists and destination paths, plus optional host-specific destination rules. Apply rules only to new tasks; existing tasks retain their category unless explicitly changed.
5. **Acceptance**: migrate an old database, create/edit/restart downloads, verify queue order and per-queue/global limits, and ensure category/destination changes never silently overwrite files.

## Existing capability — HTTP segment scheduling
- **Already implemented:** for a range-capable known-size file, the engine partitions the file into up to 32 stable byte ranges (subject to a 1 MiB minimum target range size), runs 1–8 concurrent workers, and each worker claims the next pending range as soon as it finishes its current range. Completed range files are retained for resume.
- **Not the same as IDM's adaptive algorithm:** Beatit does not dynamically split the largest remaining range while a download is already running. Treat adaptive range splitting as an optional later optimization, not as an entirely missing segmented-download feature.

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
- HTTP/HTTPS, BitTorrent, and yt-dlp media are in scope; FTP/FTPS are not.
- Private-server credentials and saved site logins are not supported; do not add credential fields or credential storage.
- Preserve existing history/settings via additive SQLite migrations.
- Never silently discard or reuse incompatible partial-download segments after URL/range-layout changes.
- Do not claim release readiness until a fresh Windows CI build and real-world pause/resume, proxy, browser-capture, torrent, and installer tests pass.
