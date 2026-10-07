param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path $Root).Path

$required = @(
  "BeatitDownloadManager.exe",
  "BeatitBrowserHost.exe",
  "browser/extension/manifest.json",
  "browser/extension/background.js",
  "tools/yt-dlp.exe",
  "tools/ffmpeg.exe",
  "tools/ffprobe.exe",
  "tools/deno.exe"
)

foreach ($relative in $required) {
    $path = Join-Path $root $relative
    if (-not (Test-Path $path -PathType Leaf)) { throw "Missing release file: $relative" }
}

$scheduler = Get-Content (Join-Path $Root "src/core/Scheduler.cpp") -Raw
if ($scheduler -notmatch "overnight|dayOfWeek") {
    throw "Scheduler implementation sanity check failed."
}
$checksum = Get-Content (Join-Path $Root "src/core/HttpDownloader.cpp") -Raw
if ($checksum -notmatch "QCryptographicHash|verifySha256|MAX_RECV_SPEED_LARGE") {
    throw "HTTP integrity/rate-limit implementation sanity check failed."
}

$manifest = Get-Content (Join-Path $root "browser/extension/manifest.json") -Raw | ConvertFrom-Json
if ($manifest.manifest_version -ne 3) { throw "Browser extension is not MV3." }
if (-not ($manifest.permissions -contains "nativeMessaging")) { throw "nativeMessaging permission missing." }
if (-not $manifest.key) { throw "Stable Chromium extension key missing." }

& (Join-Path $root "BeatitDownloadManager.exe") --version
if ($LASTEXITCODE -ne 0) { throw "Beatit --version failed." }

$files = Get-ChildItem $root -File -Recurse
if (-not $files) { throw "No release files found." }
$files | Get-FileHash -Algorithm SHA256 | Out-Null

Write-Host "Beatit release smoke test passed."
