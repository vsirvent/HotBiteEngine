<#
.SYNOPSIS
    Replays every .rdc in a directory through qrenderdoc's Python API and prints a
    per-pass GPU timing report.

.DESCRIPTION
    Each capture is replayed by analyze-capture.py into a <name>.json of per-event GPU
    durations, then report.py aggregates them into a pass table.

    Two things about qrenderdoc drive the odd plumbing here: it is a GUI process, so its
    stdout is detached and it never exits on its own (we poll a done-marker and kill it),
    and --python does not forward extra argv (parameters go through the HOTBITE_RDC_JOB
    environment variable).

    Replaying a ~1 GB capture takes a few minutes -- FetchCounters replays the frame
    repeatedly and every event needs a SetFrameEvent for its pipeline state.

.EXAMPLE
    .\analyze-captures.ps1 -Dir C:\tmp\prof
#>
[CmdletBinding()]
param(
    # Directory holding the .rdc files (the -OutDir given to capture-frames.ps1).
    [Parameter(Mandatory)][string]$Dir,
    # Where the built .cso files live; shader bytecode is matched against these to
    # recover pass names. Must be the config you captured.
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string]$QRenderDoc = 'C:\Program Files\RenderDoc\qrenderdoc.exe',
    # Per-capture replay timeout.
    [int]$TimeoutSec = 900,
    # Skip replay and just re-print the report from existing .json files.
    [switch]$ReportOnly
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$Dir = (Resolve-Path $Dir).Path
$py = Join-Path $PSScriptRoot 'analyze-capture.py'

if (-not $ReportOnly) {
    if (-not (Test-Path $QRenderDoc)) { throw "qrenderdoc not found at $QRenderDoc" }

    $caps = Get-ChildItem $Dir -Filter *.rdc | Sort-Object Name
    if (-not $caps) { throw "No .rdc files in $Dir" }

    foreach ($cap in $caps) {
        $out = Join-Path $Dir ($cap.BaseName + '.json')
        $log = Join-Path $Dir 'analyze.log'
        $done = Join-Path $Dir 'analyze.done'
        $job = Join-Path $Dir 'analyze-job.json'
        Remove-Item $done, $log -ErrorAction SilentlyContinue
        # WriteAllText, not Out-File: analyze-capture.py reads it as utf-8-sig anyway,
        # but this keeps the job file BOM-free.
        [IO.File]::WriteAllText($job, (@{
                    cap = $cap.FullName; out = $out; log = $log; done = $done
                } | ConvertTo-Json))

        Write-Host "Replaying $($cap.Name) ($([int]($cap.Length/1MB)) MB)..." -ForegroundColor Cyan
        $env:HOTBITE_RDC_JOB = $job
        $proc = Start-Process $QRenderDoc -ArgumentList '--python', $py -PassThru
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline -and -not (Test-Path $done)) { Start-Sleep -Seconds 5 }
        $ok = Test-Path $done
        if (-not $proc.HasExited) { $proc.Kill() }   # qrenderdoc never exits by itself
        if (-not $ok) { throw "Replay of $($cap.Name) timed out after ${TimeoutSec}s. Log:`n$(Get-Content $log -Raw -ErrorAction SilentlyContinue)" }
        Get-Content $log -Tail 2
    }
}

Write-Host ''
& python (Join-Path $PSScriptRoot 'report.py') $Dir (Join-Path $repo "Solution\x64\$Config")
