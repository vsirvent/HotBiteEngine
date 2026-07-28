<#
.SYNOPSIS
    Launches the Scene Editor with RenderDoc enabled and captures N frames of a level.

.DESCRIPTION
    Always profile the Release build -- Debug timings are meaningless. The editor is
    launched from its output directory so relative asset paths resolve, given a few
    seconds to settle (startup runs a camera-orbit motion blur and the GI ReSTIR caches
    need ~1s to converge after a camera jump), then asked for one capture at a time.

    Captures are ~1 GB each; four is usually enough. Take several, never one: the demo
    scene animates, so the volumetric/GI/ray-trace passes swing 2-4x frame to frame.

.EXAMPLE
    .\capture-frames.ps1 -OutDir C:\tmp\prof -Count 4 `
        -CameraPos '-54 16 -54' -CameraTarget '-60 12.6 -61'
#>
[CmdletBinding()]
param(
    # Where the .rdc files and the automation channel live.
    [Parameter(Mandatory)][string]$OutDir,
    # Level to profile. Defaults to the demo scene.
    [string]$Level,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [int]$Count = 4,
    # Optional viewport camera placement, "x y z" each. The demo scene's default camera
    # starts buried inside the lava surface, which is not a representative frame.
    [string]$CameraPos,
    [string]$CameraTarget,
    # Seconds to let the editor settle before the first capture.
    [int]$WarmupSec = 15,
    [int]$IntervalSec = 4,
    # Stop the sky clock and pin the sun at noon before capturing. The sun's angle drives
    # the volumetric, GI and shadow passes, so on a running clock the same camera profiles
    # differently minute to minute -- pass this whenever the captures are one side of a
    # before/after comparison. Cloud density is deliberately left alone: zeroing it (what
    # a screenshot A/B does) would take the cloud layer out of SkyPS and profile a frame
    # the engine never renders.
    [switch]$Freeze,
    # Leave the editor running afterwards (to poke at it by hand).
    [switch]$KeepOpen
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$cli = Join-Path $repo 'Tools\SceneEditor\automation\editor-cli.ps1'
$binDir = Join-Path $repo "Solution\x64\$Config"
$exe = Join-Path $binDir 'SceneEditor.exe'

if (-not (Test-Path $exe)) { throw "$exe not found -- build the $Config x64 SceneEditor first." }
if (-not $Level) { $Level = Join-Path $repo 'Tests\DemoGame\demo_game_scene.json' }
if (-not (Test-Path $Level)) { throw "Level not found: $Level" }
if (-not (Test-Path 'C:\Program Files\RenderDoc\renderdoc.dll')) {
    throw 'RenderDoc not installed at C:\Program Files\RenderDoc (winget install RenderDoc.RenderDoc).'
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
# The .json replay results go too, not just the .rdc: report.py aggregates every .json
# in the directory, so a leftover pair from an earlier session is silently averaged into
# this one's table -- and having been captured against a different build, its shader
# hashes no longer resolve, so it shows up as a second set of `PS:<hash>` rows rather
# than as an obvious duplicate.
#
# Where-Object rather than -Exclude: combined with -Filter, -Exclude silently matches
# nothing, so the stale files would survive the "cleanup" without a word.
Get-ChildItem $OutDir -Filter *.rdc -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $OutDir -Filter *.json -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne 'analyze-job.json' } | Remove-Item -Force

Write-Host "Launching $Config SceneEditor with RenderDoc..." -ForegroundColor Cyan
# --renderdoc must be honoured before the D3D11 device exists; the editor handles that.
Start-Process -WorkingDirectory $binDir $exe `
    -ArgumentList '--automation', $OutDir, '--level', (Resolve-Path $Level).Path, '--renderdoc'

Write-Host "Waiting ${WarmupSec}s for the scene to settle..."
Start-Sleep -Seconds $WarmupSec

# -join first: editor-cli returns an array of lines, and -match on an array filters
# instead of returning a bool, so the guard would misfire on the multi-line response.
$state = (& $cli -Dir $OutDir -Command 'state') -join "`n"
if ($state -notmatch '"level_loaded":true') {
    throw "Editor did not load the level. Response:`n$state"
}

if ($Freeze) {
    & $cli -Dir $OutDir -Command "set_component Sky Sky ""{'second_speed':0,'second_of_day':43200}""" | Out-Null
    Write-Host 'Sky clock frozen at noon.'
}

if ($CameraPos -or $CameraTarget) {
    $cam = @()
    if ($CameraPos) { $cam += "camera_pos $CameraPos" }
    if ($CameraTarget) { $cam += "camera_target $CameraTarget" }
    & $cli -Dir $OutDir -Command $cam | Out-Null
    Write-Host 'Camera placed; letting GI converge...'
    Start-Sleep -Seconds 8
}

for ($i = 1; $i -le $Count; $i++) {
    Write-Host "  capture $i/$Count"
    & $cli -Dir $OutDir -Command 'rdoc_capture' | Out-Null
    Start-Sleep -Seconds $IntervalSec
}

$last = & $cli -Dir $OutDir -Command 'rdoc_last'
Write-Host $last

if (-not $KeepOpen) { & $cli -Dir $OutDir -Command 'quit' | Out-Null }

$rdc = Get-ChildItem $OutDir -Filter *.rdc | Sort-Object Name
Write-Host "`n$($rdc.Count) captures in $OutDir" -ForegroundColor Green
$rdc | Select-Object Name, @{n = 'MB'; e = { [int]($_.Length / 1MB) } } | Format-Table
