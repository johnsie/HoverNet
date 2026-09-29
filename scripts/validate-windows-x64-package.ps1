param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseDir,
    [switch]$RequireVCRedist
)

$ErrorActionPreference = "Stop"
$release = (Resolve-Path $ReleaseDir).Path
$required = @(
    "HoverNet.exe",
    "ObjFac1.dll",
    "SDL2.dll",
    "NetTarget\ObjFac1.dat",
    "NetTarget\LinuxClient\assets\menu-hovercraft.bmp"
)

if ($RequireVCRedist) {
    $required += "vc_redist.x64.exe"
}

foreach ($relativePath in $required) {
    $path = Join-Path $release $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing x64 package file: $relativePath"
    }
    if ((Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Empty x64 package file: $relativePath"
    }
}

$tracks = @(Get-ChildItem -LiteralPath (Join-Path $release "NetTarget\Tracks") -Filter "*.trk" -File)
if ($tracks.Count -lt 4) {
    throw "Expected at least four bundled tracks, found $($tracks.Count)"
}

function Get-PeMachine([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a PE executable: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt ($stream.Length - 6)) {
            throw "Invalid PE header offset in $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Missing PE signature in $Path"
        }
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

foreach ($relativePath in @("HoverNet.exe", "ObjFac1.dll", "SDL2.dll")) {
    $path = Join-Path $release $relativePath
    $machine = Get-PeMachine $path
    if ($machine -ne 0x8664) {
        throw "$relativePath is not AMD64 (PE machine 0x$($machine.ToString('X4')))"
    }
}

Write-Host "Validated x64 HoverNet package: $($tracks.Count) tracks and AMD64 runtime binaries"
