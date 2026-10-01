# Windows client acceptance beyond "it compiles": local races on every bundled
# track, and the whole community-track download path (curl.exe spawned by the game,
# gzip decode, SHA-256 check, atomic install) against a local HTTP server.
#
#   powershell -File scripts\test-windows-client.ps1 -Exe path\to\HoverNet.exe `
#       -DataDir C:\path\to\repo-or-install-root
#
# DataDir must contain NetTarget\ObjFac1.dat, NetTarget\Tracks\*.trk (the game's
# data tree); it is passed to the game as HOVERNET_DATA_DIR. Needs curl.exe (Windows
# 10 / Server 2019 and later) and python on PATH for the local HTTP server.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$DataDir,
    [int]$Port = 18097
)
$ErrorActionPreference = "Stop"
$env:SDL_VIDEODRIVER = "dummy"
$env:SDL_AUDIODRIVER = "dummy"
$env:HOVERNET_DATA_DIR = $DataDir
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("hovernet-win-test-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $work | Out-Null
$env:APPDATA = Join-Path $work "appdata"   # isolate saved settings from the real user's
New-Item -ItemType Directory -Force -Path $env:APPDATA | Out-Null
$failures = @()

function Run-Client([string[]]$Arguments, [string]$Label, [int]$TimeoutSeconds = 120) {
    $log = Join-Path $work (($Label -replace '[^A-Za-z0-9]', '_') + ".log")
    # Start-Process joins an argument array without quoting, so a track name with a
    # space ("Metro Spiral") would arrive as two arguments.
    $argumentLine = ($Arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $process = Start-Process -FilePath $Exe -ArgumentList $argumentLine -WorkingDirectory $DataDir `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -NoNewWindow
    # Touching Handle makes ExitCode available after a timed WaitForExit (otherwise it
    # reads back empty for a process started with -NoNewWindow).
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        $script:failures += "$Label timed out"
        return $null
    }
    $text = (Get-Content $log -Raw -ErrorAction SilentlyContinue) + (Get-Content "$log.err" -Raw -ErrorAction SilentlyContinue)
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $text }
}

function Expect([bool]$Condition, [string]$Message) {
    if ($Condition) { Write-Host "  ok: $Message" } else { Write-Host "  FAIL: $Message"; $script:failures += $Message }
}

# ---- 1. local race on every bundled track -----------------------------------
Write-Host "== local races on the bundled tracks"
foreach ($track in (Get-ChildItem (Join-Path $DataDir "NetTarget\Tracks") -Filter *.trk)) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($track.Name)
    $result = Run-Client @("--autoplay", "--track", $name, "--frames", "60") "race-$name"
    Expect ($null -ne $result -and $result.ExitCode -eq 0 -and $result.Output -match "3D view:") "race starts and renders on '$name'"
}

# ---- 2. the community-track download path -----------------------------------
Write-Host "== community track download"
$host_ = Join-Path $work "host"; $shard = Join-Path $host_ "test-shard"
New-Item -ItemType Directory -Force -Path $shard | Out-Null
$sourceTrack = Join-Path $DataDir "NetTarget\Tracks\Switchback.trk"
$bytes = [System.IO.File]::ReadAllBytes($sourceTrack)
$sha = (Get-FileHash $sourceTrack -Algorithm SHA256).Hash.ToLower()
$asset = $sha.Substring(0, 16)
$gzPath = Join-Path $shard "$asset.trk.gz"
$fs = [System.IO.File]::Create($gzPath)
$gz = New-Object System.IO.Compression.GZipStream($fs, [System.IO.Compression.CompressionLevel]::Optimal)
$gz.Write($bytes, 0, $bytes.Length); $gz.Dispose(); $fs.Dispose()
$gzBytes = (Get-Item $gzPath).Length
$trackName = "Zz Windows Test Track"
$manifest = Join-Path $work "manifest.tsv"
"$trackName`trace`t10`t12`t0`t$($bytes.Length)`t$sha`ttest-shard`t$asset`t$gzBytes" | Set-Content -Encoding ASCII $manifest
$tracksDir = Join-Path $work "community"
$env:HOVERNET_COMMUNITY_MANIFEST = $manifest
$env:HOVERNET_COMMUNITY_TRACKS_DIR = $tracksDir
$env:HOVERNET_TRACK_DOWNLOAD_URL = "http://127.0.0.1:$Port"

$server = Start-Process -FilePath "python" -ArgumentList "-m", "http.server", "$Port", "--bind", "127.0.0.1" `
    -WorkingDirectory $host_ -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 2
    $result = Run-Client @("--download-community-tracks", $trackName) "download"
    Expect ($null -ne $result -and $result.ExitCode -eq 0) "--download-community-tracks exits 0"
    $installed = Join-Path $tracksDir "$trackName.trk"
    Expect (Test-Path $installed) "the track was installed"
    if (Test-Path $installed) {
        Expect ((Get-FileHash $installed -Algorithm SHA256).Hash.ToLower() -eq $sha) "the installed file is byte-identical to the original"
    }
    Expect (-not (Get-ChildItem $tracksDir -Filter *.part -ErrorAction SilentlyContinue)) "no temporary files are left behind"

    $result = Run-Client @("--download-community-tracks", $trackName) "download-again"
    Expect ($null -ne $result -and $result.Output -match "already installed") "a second run finds nothing to download"

    Remove-Item $installed -Force
    $result = Run-Client @("--autoplay", "--track", $trackName, "--frames", "40") "track-autodownload"
    Expect ($null -ne $result -and $result.ExitCode -eq 0 -and $result.Output -match "3D view:") "--track downloads a missing community track and plays it"

    # A hash mismatch must be refused and install nothing.
    Remove-Item $installed -Force
    $bad = ((Get-Content $manifest -Raw) -replace $sha, ("0" * 64))
    $badManifest = Join-Path $work "bad-manifest.tsv"
    Set-Content -Encoding ASCII $badManifest $bad
    $env:HOVERNET_COMMUNITY_MANIFEST = $badManifest
    $result = Run-Client @("--download-community-tracks", $trackName) "download-tampered"
    Expect ($null -ne $result -and $result.ExitCode -ne 0) "a download that fails verification fails"
    Expect (-not (Test-Path $installed)) "a tampered download is not installed"
    $env:HOVERNET_COMMUNITY_MANIFEST = $manifest
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}

# Server gone: a clear failure, not a hang or a crash.
$env:HOVERNET_TRACK_DOWNLOAD_URL = "http://127.0.0.1:1"
$result = Run-Client @("--download-community-tracks", $trackName) "download-no-server"
Expect ($null -ne $result -and $result.ExitCode -ne 0 -and $result.Output -match "Download failed") "an unreachable server is reported"

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "FAILED: $($failures.Count) check(s)"
    $failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}
Write-Host ""
Write-Host "Windows client acceptance passed"
