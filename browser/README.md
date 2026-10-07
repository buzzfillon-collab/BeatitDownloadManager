# Beatit browser integration

The browser integration uses WebExtension Manifest V3 plus native messaging.

Capture targets:
- ordinary HTTP/HTTPS URLs
- direct TS media
- HLS M3U8 playlists
- YouTube page URLs

DRM/encrypted streams are intentionally not bypassed.

The native host is BeatitBrowserHost.exe. It receives browser native-messaging JSON framing and forwards the payload to the running Beatit process over a local Qt socket.

On Windows, the installer will register the native host under the browser's NativeMessagingHosts registry key. Chrome requires the manifest allowed_origins entry to contain the exact installed extension ID; Firefox uses allowed_extensions.

For development, load browser/extension as an unpacked extension and install a development native-messaging manifest with its generated extension ID.

HLS and YouTube are routed through the external yt-dlp backend. Portable builds bundle yt-dlp.exe (nightly by default), ffmpeg.exe, ffprobe.exe, and Deno for YouTube JavaScript extraction. Beatit checks yt-dlp once per day and can switch between nightly and stable. If extraction fails, it updates yt-dlp and retries the download once. Direct media URLs use Beatit's HTTP engine.
