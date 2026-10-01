# Installs the community track pack for the current user (no administrator needed).
#
#   powershell -File scripts\install-community-tracks.ps1 -Source PACK.zip
#   powershell -File scripts\install-community-tracks.ps1 -Source C:\path\to\trk-folder
#
# HoverNet looks for community tracks in %APPDATA%\HoverNet\CommunityTracks, or in
# NetTarget\CommunityTracks beside HoverNet.exe.
param([Parameter(Mandatory = $true)][string]$Source)
$ErrorActionPreference = "Stop"

$target = Join-Path $env:APPDATA "HoverNet\CommunityTracks"
New-Item -ItemType Directory -Force -Path $target | Out-Null

if (Test-Path -LiteralPath $Source -PathType Container) {
    $tracks = $Source
} else {
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ("hovernet-pack-" + [guid]::NewGuid())
    Expand-Archive -LiteralPath $Source -DestinationPath $work
    $found = Get-ChildItem -LiteralPath $work -Directory -Recurse |
        Where-Object { $_.Name -in @("CommunityTracks", "tracks") } | Select-Object -First 1
    if (-not $found) { throw "No CommunityTracks (or tracks) folder found inside $Source" }
    $tracks = $found.FullName
}

Copy-Item -Path (Join-Path $tracks "*.trk") -Destination $target -Force
$count = @(Get-ChildItem -LiteralPath $target -Filter "*.trk" -File).Count
Write-Host "Installed $count community tracks into $target"
