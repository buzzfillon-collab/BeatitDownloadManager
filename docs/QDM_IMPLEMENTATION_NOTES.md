# QDM implementation notes

Beatit is a native Qt/C++ Windows application. Quantum Download Manager (QDM) is a Rust/Tauri application released under the MIT License. QDM is a useful public reference for download-manager behaviour; Beatit keeps its existing libcurl, SQLite, libtorrent, Qt, and yt-dlp architecture rather than introducing a second desktop runtime.

Upstream reference: https://github.com/PBhadoo/QDM

## Feature mapping

| QDM capability | Beatit implementation |
|---|---|
| Multi-segment HTTP | libcurl byte-range workers; up to 32 stable on-disk ranges, 1–8 configurable workers |
| Persistent pause/resume | Per-range files plus persisted SQLite task state; completed ranges are reused after restart |
| Automatic retries | Up to five attempts for transient segment/network failures, exponential backoff from 0.5 to 8 seconds; permanent HTTP failures stop immediately |
| Browser interception and context menu | Chromium MV3 / Firefox WebExtension native messaging, then newline-framed Qt local-socket IPC |
| HLS and DASH | Browser detects .m3u8 and .mpd manifests and routes them to yt-dlp; yt-dlp is configured for continuation, retries, and concurrent fragment downloads |
| Media muxing | Existing FFmpeg toolchain and yt-dlp merge-output options |

## Why not copy QDM's Rust engine wholesale?

QDM's engine is written for its Rust/Tauri application and has different task-state, persistence, and UI contracts. Dropping that engine into Beatit would require a new process boundary and duplicate state handling. Beatit already has a working C++/Qt transfer engine, so the changes above target behavioural parity while preserving the current architecture. yt-dlp is used for broad HLS/DASH and site extraction rather than duplicating a media parser that upstream already maintains.

No QDM source files are copied into Beatit by the changes documented here. If code is copied or adapted from QDM in a future change, preserve the upstream MIT copyright and licence notice in the relevant source distribution.

## Verification still required

These are implementation targets, not a claim that every scenario has already passed a clean-machine test.

- [ ] Build the latest main branch in Windows x64 CI.
- [ ] Verify the browser extension can launch Beatit when closed, send a URL, and receive the framed native-messaging response.
- [ ] Test browser requests arriving in multiple local-socket chunks and verify no partial JSON is processed.
- [ ] Interrupt a segmented download during active ranges, restart Beatit, and verify retained ranges are reused without output corruption.
- [ ] Force transient HTTP 5xx/429/network failures and confirm bounded retries; verify 401/403/404 fail without repeated retries.
- [ ] Test a server that ignores Range and confirm Beatit reports a clear failure rather than assembling invalid content.
- [ ] Test direct HLS and DASH manifests with yt-dlp/FFmpeg installed, including fragment retry and final media validation.
- [ ] Test a signed URL that expires mid-download; refresh-link support remains a separate feature, not guaranteed by generic retries.
