# Runs SceneEditor under cdb (console debugger from the WinDbg package) so that if
# it crashes, $LogPath ends with a full analysis: exception details, faulting stack
# with source file:line (PDB next to the exe), and every thread's stack.
#
# The editor runs normally under the debugger (automation channel included), so the
# usual editor-cli.ps1 flow works while this is running. On a clean exit the log
# ends with the process exit code instead. Intended to be started in the background:
#
#   .\debug-run.ps1 -AutomationDir C:\tmp\ed -Level C:\proj\...\level.json
#   ... drive with editor-cli.ps1, on crash/exit read debug-run.log in the automation dir
param(
    [string]$AutomationDir,
    [string]$Level,
    [string]$Project,
    [string]$Config = 'Debug',
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$exeDir = Join-Path $repoRoot "Solution\x64\$Config"
$exe = Join-Path $exeDir 'SceneEditor.exe'
if (-not (Test-Path $exe)) {
    Write-Error "Not built: $exe"
}

$pkg = Get-AppxPackage -Name Microsoft.WinDbg
if ($null -eq $pkg) {
    Write-Error "WinDbg package not installed (winget install Microsoft.WinDbg) - cdb.exe unavailable."
}
$cdb = Join-Path $pkg.InstallLocation 'amd64\cdb.exe'

if (-not $LogPath) {
    $base = if ($AutomationDir) { $AutomationDir } else { $exeDir }
    if (-not (Test-Path $base)) { New-Item -ItemType Directory -Force $base | Out-Null }
    $LogPath = Join-Path $base 'debug-run.log'
}
Remove-Item $LogPath -Force -ErrorAction SilentlyContinue

$appArgs = @()
if ($AutomationDir) { $appArgs += @('--automation', "`"$AutomationDir`"") }
if ($Level) { $appArgs += @('--level', "`"$Level`"") }
if ($Project) { $appArgs += @('--project', "`"$Project`"") }

# -g/-G: skip initial/final breakpoints; -lines: source line info.
# The -c chain: run; when g returns we are either at a second-chance exception
# (crash -> analyze + all stacks) or at process exit (commands fail harmlessly); quit.
$cdbArgs = @('-g', '-G', '-lines', '-logo', $LogPath,
    '-c', '.lines; g; .echo === DEBUGGER BREAK (crash) ===; !analyze -v; .echo === faulting stack ===; kv; .echo === all threads ===; ~*kv; q',
    $exe) + $appArgs

Set-Location $exeDir  # relative asset paths resolve like a normal run
& $cdb @cdbArgs
"cdb exited with $LASTEXITCODE. Log: $LogPath"
