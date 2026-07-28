# Runs the SceneEditor / engine regression suites.
#
#   .\Run-Tests.ps1                      # everything, Release
#   .\Run-Tests.ps1 -Suite '*template*'  # one file
#   .\Run-Tests.ps1 -Test '*undo*'       # one test, across files
#   .\Run-Tests.ps1 -ListSuites
#
# Each suite gets its own generated project and its own editor process: the
# editor loads one level per session (SceneEditorApp::OpenLevel refuses a
# second), and a fresh process is also what keeps a suite that saves, imports or
# deletes from reaching the next one. Cost is a few seconds of level load per
# suite, which is why suites are grouped by subject rather than split finely.
#
# Exit code is the number of failed tests, so CI can gate on it.

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string]$Exe,
    [string]$Suite = '*',
    [string]$Test = '*',
    [string]$OutputDir,
    # Keeps the generated projects, screenshots and automation dirs after the run.
    [switch]$Keep,
    [switch]$ListSuites,
    [int]$StartTimeoutSec = 240
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))

if (-not $Exe) {
    $Exe = Join-Path $repoRoot "Solution\x64\$Config\SceneEditor.exe"
}
$suiteFiles = @(Get-ChildItem (Join-Path $PSScriptRoot 'suites') -Filter '*.tests.ps1' |
    Where-Object { $_.Name -like $Suite -or $_.BaseName -like $Suite } | Sort-Object Name)

if ($ListSuites) {
    foreach ($f in $suiteFiles) {
        $header = Get-Content $f.FullName -TotalCount 12
        $fixture = 'empty'
        $fm = $header | Select-String -Pattern '^#\s*fixture:\s*(\S+)'
        if ($fm) { $fixture = $fm.Matches.Groups[1].Value }
        $desc = ''
        $dm = $header | Select-String -Pattern '^#\s*description:\s*(.+)$'
        if ($dm) { $desc = $dm.Matches.Groups[1].Value }
        "{0,-34} fixture={1,-7} {2}" -f $f.Name, $fixture, $desc
    }
    return
}

if (-not (Test-Path $Exe)) {
    throw "SceneEditor.exe not found at $Exe. Build it first:`n" +
          "  & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' " +
          "Solution\HotBiteEngine.sln /m /t:SceneEditor /p:Configuration=$Config /p:Platform=x64"
}
if ($suiteFiles.Count -eq 0) {
    throw "no suites matched -Suite '$Suite'."
}

if (-not $OutputDir) {
    $OutputDir = Join-Path $env:TEMP ("HotBiteEditorTests\" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null

Import-Module (Join-Path $PSScriptRoot 'TestFramework.psm1') -Force -DisableNameChecking
Set-TestNameFilter -Filter $Test
Reset-TestResults

$newProject = Join-Path $PSScriptRoot 'New-TestProject.ps1'

# Runs one suite file against a live editor. The suite's Test bodies are
# scriptblocks defined while this function is in scope, so they see $Session,
# $Project, $LevelPath, $ShotDir and the two senders below - and nothing from the
# previous suite, because this scope is new each time.
function Invoke-TestSuite {
    param([string]$File, [string]$Fixture, [string]$WorkDir)

    $Project = Join-Path $WorkDir 'project'
    $LevelPath = & $newProject -Kind $Fixture -Path $Project
    $Assets = Join-Path $Project 'Assets'
    $ShotDir = Join-Path $WorkDir 'shots'
    New-Item -ItemType Directory -Force $ShotDir | Out-Null

    $Session = New-EditorSession -Exe $script:Exe -Level $LevelPath `
        -AutomationDir (Join-Path $WorkDir 'channel') -StartTimeoutSec $script:StartTimeoutSec

    # Sends a batch and returns one result object per command. Commands are one
    # array argument - `Send 'a', 'b'` - never separate arguments: PowerShell 5.1
    # stringifies an array bound through ValueFromRemainingArguments, which would
    # silently glue the batch into a single unparseable line.
    # The `,` on the return keeps a one-command batch an array rather than
    # letting the pipeline unwrap it to a bare result object.
    function Send {
        param([Parameter(Mandatory = $true, Position = 0)][string[]]$Command)
        $out = @(Invoke-EditorCommand -Session $Session -Command $Command)
        return , $out
    }
    # The same, asserting every command in the batch answered OK. Most setup
    # lines are this: a failure names the command instead of surfacing three
    # commands later as a confusing assertion.
    function SendOk {
        param([Parameter(Mandatory = $true, Position = 0)][string[]]$Command)
        $out = @(Invoke-EditorCommand -Session $Session -Command $Command)
        Assert-Ok -Result $out
        return , $out
    }
    # Captures the frame into the suite's shot folder and returns the path.
    function Shot {
        param([string]$Name)
        $path = Join-Path $ShotDir ($Name + '.png')
        $r = Invoke-EditorCommand -Session $Session -Command "screenshot $path"
        Assert-Ok -Result $r
        return $path
    }

    try {
        . $File
    }
    finally {
        Close-EditorSession -Session $Session
    }
}

$started = Get-Date
Write-Host ""
Write-Host "SceneEditor automation tests" -ForegroundColor Cyan
Write-Host "  exe    : $Exe"
Write-Host "  output : $OutputDir"
Write-Host ""

foreach ($file in $suiteFiles) {
    $header = Get-Content $file.FullName -TotalCount 12
    $fixtureMatch = $header | Select-String -Pattern '^#\s*fixture:\s*(\S+)'
    $fixture = 'empty'
    if ($fixtureMatch) { $fixture = $fixtureMatch.Matches.Groups[1].Value }

    Set-CurrentSuite -Name $file.BaseName
    Write-Host ("  {0} (fixture: {1})" -f $file.Name, $fixture) -ForegroundColor White

    $workDir = Join-Path $OutputDir $file.BaseName
    New-Item -ItemType Directory -Force $workDir | Out-Null
    try {
        Invoke-TestSuite -File $file.FullName -Fixture $fixture -WorkDir $workDir
    }
    catch {
        # A suite that cannot start (editor crash on load, missing assets) counts
        # as one failure rather than aborting the run - the other suites still say
        # something useful about where the damage is.
        Write-Host ("    [FAIL] <suite did not run> {0}" -f $_.Exception.Message) -ForegroundColor Red
        Add-TestFailure -Suite $file.BaseName -Name '<suite did not run>' -Message $_.Exception.Message
    }
}

$results = Get-TestResults
$passed = @($results | Where-Object { $_.Status -eq 'PASS' }).Count
$failed = @($results | Where-Object { $_.Status -eq 'FAIL' }).Count
$skipped = @($results | Where-Object { $_.Status -eq 'SKIP' }).Count
$elapsed = ((Get-Date) - $started).TotalSeconds

Write-Host ""
if ($failed -gt 0) {
    Write-Host "Failures:" -ForegroundColor Red
    foreach ($r in ($results | Where-Object { $_.Status -eq 'FAIL' })) {
        Write-Host ("  {0} / {1}" -f $r.Suite, $r.Name) -ForegroundColor Red
        Write-Host ("      {0}" -f $r.Message) -ForegroundColor DarkRed
    }
    Write-Host ""
}
$colour = 'Green'
if ($failed -gt 0) { $colour = 'Red' }
Write-Host ("{0} passed, {1} failed, {2} skipped in {3:N1}s" -f $passed, $failed, $skipped, $elapsed) -ForegroundColor $colour

$results | Export-Csv -Path (Join-Path $OutputDir 'results.csv') -NoTypeInformation -Encoding UTF8

if (-not $Keep -and $failed -eq 0) {
    Remove-Item $OutputDir -Recurse -Force -ErrorAction SilentlyContinue
}
else {
    Write-Host "Artifacts kept in $OutputDir"
}

exit $failed
