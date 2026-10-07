# Windows installer

The installer is built with Inno Setup and targets Windows x64.

It installs Beatit per-user under %LOCALAPPDATA%/Programs/Beatit, registers magnet: and .torrent associations, creates Start Menu integration, and includes the complete portable runtime including the browser host and bundled yt-dlp/FFmpeg/Deno toolchain.

Browser extensions cannot be silently installed by an ordinary desktop installer. The installer therefore registers the native messaging host automatically, while the bundled browser/extension directory is loaded into the target browser as an unpacked extension during the beta phase.

The installer is intentionally unsigned until a real Authenticode certificate is available.
