<#
.SYNOPSIS
    What each pass of a frame costs under two or more settings of the same scene, interleaved: the GPU's own timestamp queries
    (`gpu_timings`), no RenderDoc.

.DESCRIPTION
    capture-frames.ps1 + analyze-captures.ps1 say where a frame's time goes down to the shader, in minutes per capture. This says
    how much a pass costs with one setting against another in a couple of minutes, which is what an A/B of an option wants (the
    hardware or the software ray tracer, a quality, a shader) and is the one to reach for first.

    It launches the Scene Editor on a level, places the camera, runs the setup commands, and then goes round the variants `Rounds`
    times: each turn sends the variant's commands, lets `Warmup` frames go by (pipelines, the temporal history), and reads the
    cumulative per-pass sums of `gpu_timings` before and after `Frames` more. Interleaving is the point: the GPU's clocks, the
    other things on the machine and the scene's own motion drift over minutes, so one variant measured, then the other, measures
    the drift. Each variant's number is the mean of its turns, and the spread of the turns is printed so a difference smaller than
    it is not believed.

    The numbers are milliseconds a frame summed over the passes `gpu_timings` names (see its entry in ../README.md: `rt`
    reflections and refractions, `gi` indirect light, `light` the deferred lighting, ...). They are the GPU's time, not the
    frame's: the frame rate depends on the window and on the queue. Close everything else that uses the GPU first.

.EXAMPLE
    # Marbles' sponza (solo level 3: ../Marbles is a sibling checkout), the hardware ray tracer against the software walk, from
    # the middle of the atrium, with the sky clock stopped:
    .\measure-gpu.ps1 -Level ..\Marbles\Marbles\Assets\Levels\Solo\3\level.json -CameraPos '0 4 -15' -CameraTarget '0 5 0' -Freeze `
        -Variants ([ordered]@{ software = 'rt_tracer sw'; hardware = 'rt_tracer hw' })
#>
[CmdletBinding()]
param(
    [string]$Level,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    # Optional camera placement, "x y z" each.
    [string]$CameraPos,
    [string]$CameraTarget,
    # Commands (one per array element) sent once before the measuring, e.g. 'render rt_quality high'.
    [string[]]$Setup = @(),
    # name -> the commands of the variant, one string, ';' between commands, as an [ordered] dictionary: the order is the order they are run in.
    [Parameter(Mandatory)][System.Collections.IDictionary]$Variants,
    [int]$Rounds = 4,
    [int]$Warmup = 60,
    [int]$Frames = 120,
    # Stop the sky clock at noon (see capture-frames.ps1: the sun drives the shadow, indirect and volumetric passes).
    [switch]$Freeze,
    # Where the automation channel lives.
    [string]$Dir = (Join-Path $env:TEMP 'hotbite-measure-gpu'),
    [switch]$KeepOpen
)

$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
Import-Module (Join-Path $repo 'Tools\SceneEditor\automation\tests\TestFramework.psm1') -Force
$exe = Join-Path $repo "Solution\x64\$Config\SceneEditor.exe"
if (-not (Test-Path $exe)) { throw "$exe not found -- build the $Config x64 SceneEditor first." }
if (-not $Level) { $Level = Join-Path $repo 'Tests\DemoGame\demo_game_scene.json' }
if (-not (Test-Path $Level)) { throw "Level not found: $Level" }
$Level = (Resolve-Path $Level).Path
if (Test-Path $Dir) { Remove-Item $Dir -Recurse -Force }

function Send {
    param([string[]]$Command)
    $results = Invoke-EditorCommand -Session $session -Command $Command
    foreach ($r in $results) {
        if ($r.Status -ne 'OK') { throw "$($r.Command): $($r.Text)" }
    }
    return $results
}

function Get-Timings {
    $text = (Send 'gpu_timings')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($text, '(\w+)=([-0-9.eE+]+)')) { $o[$m.Groups[1].Value] = [double]::Parse($m.Groups[2].Value, $invariant) }
    return $o
}

$session = New-EditorSession -Exe $exe -Level $Level -AutomationDir $Dir
try {
    $state = (Send 'state')[0].Text
    if ($state -notmatch '"level_loaded":true') { throw "the editor did not load the level: $state" }
    if ($Freeze) { Send "set_component Sky Sky ""{'second_speed':0,'second_of_day':43200}""" | Out-Null }
    if ($CameraPos) { Send "camera_pos $CameraPos" | Out-Null }
    if ($CameraTarget) { Send "camera_target $CameraTarget" | Out-Null }
    foreach ($c in $Setup) { Send $c | Out-Null }
    Step-EditorFrames -Session $session -Count 60

    $passes = 'shadows', 'depth', 'scene', 'tile_cull', 'deferred', 'motion', 'rt', 'gi', 'light', 'mix', 'aa', 'blur'
    $turns = @{}
    foreach ($name in $Variants.Keys) { $turns[$name] = @() }
    for ($round = 1; $round -le $Rounds; $round++) {
        foreach ($name in $Variants.Keys) {
            Send ($Variants[$name] -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) | Out-Null
            Step-EditorFrames -Session $session -Count $Warmup
            $a = Get-Timings
            Step-EditorFrames -Session $session -Count $Frames
            $b = Get-Timings
            $n = $b['frames'] - $a['frames']
            if ($n -lt $Frames / 2) { throw "only $n frames were timed for $name" }
            $turn = @{ frames = $n }
            foreach ($p in $passes) { if ($b.ContainsKey($p)) { $turn[$p] = ($b[$p] - $a[$p]) / $n } }
            $turns[$name] += , $turn
        }
    }

    # One row a variant: the mean over its turns, and (rt, gi) the spread of the turns, which is what a difference has to beat.
    Write-Host ''
    Write-Host ("{0,-14} {1}" -f 'ms / frame', ($passes -join '  ' | ForEach-Object { $_ })) -ForegroundColor Cyan
    $first = $null
    foreach ($name in $Variants.Keys) {
        $mean = @{}
        foreach ($p in $passes) {
            $values = @($turns[$name] | Where-Object { $_.ContainsKey($p) } | ForEach-Object { $_[$p] })
            $mean[$p] = if ($values.Count -gt 0) { ($values | Measure-Object -Average).Average } else { 0.0 }
        }
        if ($null -eq $first) { $first = $mean }
        $row = ($passes | ForEach-Object { ("{0,8:N3}" -f $mean[$_]) }) -join ' '
        Write-Host ("{0,-14} {1}" -f $name, $row)
        foreach ($p in 'rt', 'gi') {
            $values = @($turns[$name] | ForEach-Object { $_[$p] })
            $lo = ($values | Measure-Object -Minimum).Minimum
            $hi = ($values | Measure-Object -Maximum).Maximum
            $ratio = if ($first[$p] -gt 0) { $mean[$p] / $first[$p] } else { 0.0 }
            Write-Host ("{0,14} {1}: {2:N3} ms (turns {3:N3} - {4:N3}), {5:N2}x of {6}" -f '', $p, $mean[$p], $lo, $hi, $ratio, @($Variants.Keys)[0])
        }
    }
}
finally {
    if (-not $KeepOpen) { Close-EditorSession -Session $session }
}
