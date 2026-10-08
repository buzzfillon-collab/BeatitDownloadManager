# Beatit browser integration

Beatit's browser integration is a WebExtension Manifest V3 extension plus a Windows native-messaging host.

## Supported capture paths

- Ordinary HTTP/HTTPS browser downloads
- Direct media URLs
- HLS M3U8 playlists
- YouTube page URLs
- TS media URLs

DRM/encrypted streams are intentionally not bypassed.

## Installation

The portable/release build contains BeatitDownloadManager.exe, BeatitBrowserHost.exe, and browser/extension/.

On Windows, Beatit automatically registers its native-messaging host for Chrome, Edge, Chromium, Brave, Vivaldi, and Firefox when the application starts. The registration is per-user and requires no administrator rights. The manifest is regenerated on startup so moving a portable Beatit folder does not leave the registry pointing at the old executable.

The extension has a fixed development/distribution identity:

- Chromium ID: mhmokbkgppciedcjabjlhkbhipnkfkpb
- Firefox ID: beatit@example.org

Load browser/extension/ as an unpacked extension during development/testing. Chrome/Edge/Brave/Vivaldi must be given the bundled extension directory; Firefox can load the same WebExtension.

Double-clicking BeatitBrowserHost.exe is not a test: it is a native-messaging protocol endpoint and waits for the browser to send a framed message. The release build uses the Windows GUI subsystem so it does not open a visible console window when launched normally.

## Runtime flow

Browser extension -> native messaging -> BeatitBrowserHost.exe -> local Qt IPC -> running Beatit -> HTTP engine or yt-dlp.

HLS and YouTube are routed through the external yt-dlp backend. Portable builds bundle yt-dlp.exe (nightly by default), ffmpeg.exe, ffprobe.exe, and Deno for YouTube JavaScript extraction. Beatit checks yt-dlp once per day and can switch between nightly and stable. If extraction fails, it updates yt-dlp and retries the download once. Direct media URLs use Beatit's HTTP engine.


### In-page video controls

The extension injects a small **↓ Download** control on HTML5 video players. It also provides **♫ MP3** to extract audio through the bundled yt-dlp + FFmpeg toolchain.

- YouTube pages are sent to yt-dlp using the page URL.
- Generic HTML5 videos are sent using the media URL when available, otherwise the page URL.
- HLS/media captures continue to use the existing native-messaging path.
- The browser UI never performs the media extraction itself; Beatit owns the download/conversion process.
