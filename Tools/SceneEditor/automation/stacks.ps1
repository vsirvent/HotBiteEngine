# Snapshots every thread's stack of a *running* SceneEditor (or any process) without
# stopping it - non-invasive cdb attach, dump, detach. Use it to see where the editor
# is stuck when it hangs or stops responding to the automation channel.
#
#   .\stacks.ps1                 # finds the SceneEditor process automatically
#   .\stacks.ps1 -ProcessId 1234
param(
    [int]$ProcessId = 0
)

$ErrorActionPreference = 'Stop'

if ($ProcessId -eq 0) {
    $proc = Get-Process SceneEditor -ErrorAction SilentlyContinue
    if ($null -eq $proc) {
        Write-Error 'No SceneEditor process found; pass -ProcessId for a different target.'
    }
    $ProcessId = ($proc | Select-Object -First 1).Id
}

$pkg = Get-AppxPackage -Name Microsoft.WinDbg
if ($null -eq $pkg) {
    Write-Error "WinDbg package not installed (winget install Microsoft.WinDbg) - cdb.exe unavailable."
}
$cdb = Join-Path $pkg.InstallLocation 'amd64\cdb.exe'

# -pv: non-invasive attach (threads are suspended only while dumping, no debug events
# are consumed, and qd resumes the process untouched).
& $cdb -pv -p $ProcessId -lines -c '.lines; ~*kv; qd'
