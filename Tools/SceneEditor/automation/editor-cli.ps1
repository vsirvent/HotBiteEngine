# Sends one or more commands to a running SceneEditor started with --automation <Dir>
# and prints the editor's response. Each -Command element is one command line.
#
# Example session:
#   Start-Process ...\SceneEditor.exe -ArgumentList '--automation', $dir, '--level', $level
#   .\editor-cli.ps1 -Dir $dir -Command 'ping'
#   .\editor-cli.ps1 -Dir $dir -Command 'state'
#   .\editor-cli.ps1 -Dir $dir -Command 'select box1', 'set_position 0 2 0', 'screenshot C:\tmp\after.png'
#   .\editor-cli.ps1 -Dir $dir -Command 'menu "File/Save Level"', 'quit'
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(Mandatory = $true)][string[]]$Command,
    [int]$TimeoutSec = 30
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Dir)) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
}
$response = Join-Path $Dir 'response.txt'
Remove-Item $response -Force -ErrorAction SilentlyContinue

# Write-then-rename so the editor never reads a half-written command file.
$tmp = Join-Path $Dir 'command.pending'
[IO.File]::WriteAllLines($tmp, $Command)
Move-Item $tmp (Join-Path $Dir 'command.txt') -Force

$deadline = (Get-Date).AddSeconds($TimeoutSec)
while (-not (Test-Path $response)) {
    if ((Get-Date) -gt $deadline) {
        Write-Error "Timed out after ${TimeoutSec}s waiting for $response - is the editor running with --automation `"$Dir`"?"
    }
    Start-Sleep -Milliseconds 100
}
Get-Content $response
