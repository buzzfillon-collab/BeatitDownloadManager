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

$RepoRoot = Split-Path $PSScriptRoot -Parent
$scheduler = Get-Content (Join-Path $RepoRoot "src/core/Scheduler.cpp") -Raw
if ($scheduler -notmatch "overnight|dayOfWeek") {
    throw "Scheduler implementation sanity check failed."
}
$checksum = Get-Content (Join-Path $RepoRoot "src/core/HttpDownloader.cpp") -Raw
if ($checksum -notmatch "QCryptographicHash|verifySha256|MAX_RECV_SPEED_LARGE") {
    throw "HTTP integrity/rate-limit implementation sanity check failed."
}
if ($checksum -notmatch "splitRequested|muchSlower|Adaptive range integrity check failed|Adaptive range coverage check failed|schedulerDone") {
    throw "Adaptive live HTTP range splitting or coverage validation is missing."
}
$mainWindow = Get-Content (Join-Path $RepoRoot "src/app/MainWindow.cpp") -Raw
if ($mainWindow -notmatch "categories/|categoryRules/|After download completes|completion/action") {
    throw "Category routing or completion-action implementation missing."
}
if ($mainWindow -notmatch "showSelectedProperties|showLinkExtractor|Grab links") { throw "Properties dialog or link extractor UI missing." }
$downloadManager = Get-Content (Join-Path $RepoRoot "src/core/DownloadManager.cpp") -Raw
if ($downloadManager -notmatch "downloadInfo|updateProperties" -or $downloadManager -notmatch "updated.proxyType|updated.userAgent") {
    throw "Editable download properties persistence is missing."
}
$httpDownloader = Get-Content (Join-Path $RepoRoot "src/core/HttpDownloader.cpp") -Raw
if ($httpDownloader -notmatch "setUserAgent|CURLOPT_USERAGENT") {
    throw "Configurable HTTP User-Agent implementation missing."
}
if ($mainWindow -notmatch "href\\s\*=|same-host HTML pages|Download All") {
    throw "Link extraction or bounded Site Grabber implementation missing."
}
foreach ($feature in @("categories/hostRules","categories/customNames","clipboard/monitor","antivirus/program","power/shutdownOnComplete",
                       "maxDepth","includePattern","excludePattern","Save Site Grabber project","runScheduledSiteGrabber",
                       "previewRemoteZip","checkForUpdates","QCryptographicHash::hash(installer","showSyncManager","runSyncChecks",
                       "lightThemeOverrides","appearance/columns","toolbarGrabLinksButton","void MainWindow::dropEvent","class DownloadTable","setDragEnabled(true)")) {
    if (-not $mainWindow.Contains($feature)) { throw "Missing P1/P2 feature implementation marker: $feature" }
}
$installerScript = Get-Content (Join-Path $RepoRoot "installer/Beatit.iss") -Raw
if ($installerScript -notmatch 'Name: "\{autoprograms\}\\\{#MyAppName\}"') {
    throw "Installer Start Menu shortcut path is malformed."
}
if ($mainWindow -match 'ftp://') {
    throw "FTP URL intake must remain out of scope."
}
$databaseHeader = Get-Content (Join-Path $RepoRoot "src/core/DownloadDatabase.h") -Raw
$databaseSource = Get-Content (Join-Path $RepoRoot "src/core/DownloadDatabase.cpp") -Raw
foreach ($column in @("category", "description", "user_agent", "queue_id", "connection_count", "proxy_type", "proxy_host", "proxy_port")) {
    $needle = 'ensureColumn(db, "' + $column + '"'
    if (-not $databaseSource.Contains($needle)) {
        throw "SQLite migration missing column: $column"
    }
}
if ($databaseSource -notmatch "PRAGMA user_version=4" -or $databaseSource -notmatch "BEGIN IMMEDIATE" -or $databaseSource -notmatch "ROLLBACK" -or $databaseSource -notmatch "CREATE TABLE IF NOT EXISTS download_queues" -or $databaseSource -notmatch "loadQueues|saveQueue|removeQueue") {
    throw "Transactional SQLite schema migration checks failed."
}
if ($databaseHeader -match "password|credential") {
    throw "Private-server credential fields must remain out of scope."
}

$manifest = Get-Content (Join-Path $root "browser/extension/manifest.json") -Raw | ConvertFrom-Json
if ($manifest.manifest_version -ne 3) { throw "Browser extension is not MV3." }
if (-not ($manifest.permissions -contains "nativeMessaging")) { throw "nativeMessaging permission missing." }
if (-not $manifest.key) { throw "Stable Chromium extension key missing." }

$versionOutput = & (Join-Path $root "BeatitDownloadManager.exe") --version 2>&1 | Out-String
if ($versionOutput -notmatch "Beatit Download Manager") { throw "Beatit --version did not return the expected version string." }

$files = Get-ChildItem $root -File -Recurse
if (-not $files) { throw "No release files found." }
$files | Get-FileHash -Algorithm SHA256 | Out-Null

Write-Host "Beatit release smoke test passed."
