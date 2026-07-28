# Test framework for the SceneEditor automation channel.
#
# A "unit" here is one automation command (or one engine behaviour observable
# through one), driven against a real editor process holding a generated fixture
# project. There is no in-process test harness because nothing in the editor runs
# without a D3D device and a loaded World; the automation channel *is* the seam,
# and it executes the same code paths the UI does.
#
# Suites use four things from here:
#   Test <name> { ... }        declares and immediately runs one test
#   Send / SendOk              (defined by Run-Tests.ps1, they close over $Session)
#   Assert-*                   failure = a thrown message naming expected vs actual
#   Get-State / Get-Component / Get-Render / Get-Camera / Get-Position
#
# Everything is PowerShell 5.1 compatible: no ternaries, no ?., no &&.

$script:Results = @()
$script:CurrentSuite = ''
$script:SkipPrefix = 'SKIPPED::'

#--- result collection --------------------------------------------------------

function Reset-TestResults {
    $script:Results = @()
}

function Get-TestResults {
    return $script:Results
}

function Set-CurrentSuite {
    param([string]$Name)
    $script:CurrentSuite = $Name
}

# Declares one test and runs it now. A test fails by throwing - every Assert-*
# does - so the body reads as a straight line of commands and expectations.
function Test {
    param(
        [Parameter(Mandatory = $true, Position = 0)][string]$Name,
        [Parameter(Mandatory = $true, Position = 1)][scriptblock]$Body
    )
    if ($script:TestNameFilter -and $Name -notlike $script:TestNameFilter) {
        return
    }
    $started = Get-Date
    $status = 'PASS'
    $message = ''
    try {
        & $Body | Out-Null
    }
    catch {
        $text = "$($_.Exception.Message)"
        if ($text.StartsWith($script:SkipPrefix)) {
            $status = 'SKIP'
            $message = $text.Substring($script:SkipPrefix.Length)
        }
        else {
            $status = 'FAIL'
            $message = $text
            # The line inside the suite that threw is what the reader wants.
            if ($_.InvocationInfo -and $_.InvocationInfo.ScriptLineNumber) {
                $message += " (at $(Split-Path -Leaf $_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber))"
            }
        }
    }
    $elapsed = ((Get-Date) - $started).TotalSeconds
    $script:Results += [pscustomobject]@{
        Suite   = $script:CurrentSuite
        Name    = $Name
        Status  = $status
        Message = $message
        Seconds = $elapsed
    }
    switch ($status) {
        'PASS' { Write-Host ("    [ ok ] {0} ({1:N2}s)" -f $Name, $elapsed) -ForegroundColor DarkGreen }
        'SKIP' { Write-Host ("    [skip] {0} - {1}" -f $Name, $message) -ForegroundColor DarkYellow }
        'FAIL' {
            Write-Host ("    [FAIL] {0} ({1:N2}s)" -f $Name, $elapsed) -ForegroundColor Red
            Write-Host ("           {0}" -f $message) -ForegroundColor Red
        }
    }
}

function Set-TestNameFilter {
    param([string]$Filter)
    $script:TestNameFilter = $Filter
}

# Records a failure that happened outside any test - a suite whose editor never
# started, say. Without this such a suite would report zero results and the run
# would come out green.
function Add-TestFailure {
    param([string]$Suite, [string]$Name, [string]$Message)
    $script:Results += [pscustomobject]@{
        Suite   = $Suite
        Name    = $Name
        Status  = 'FAIL'
        Message = $Message
        Seconds = 0.0
    }
}

# Ends the current test as "not applicable here" rather than as a failure -
# for a fixture that is missing an optional asset, say.
function Skip-Test {
    param([string]$Reason)
    throw ($script:SkipPrefix + $Reason)
}

#--- editor session -----------------------------------------------------------

function Get-CrashReport {
    param($Session)
    $crash = Join-Path $Session.Dir 'crash.txt'
    if (Test-Path $crash) {
        return "`n--- crash.txt ---`n" + ((Get-Content $crash) -join "`n")
    }
    return ''
}

# Launches an editor on $Level with the automation channel in $AutomationDir and
# waits until it answers `ping`. The working directory has to be the output dir:
# shaders and the engine's own relative asset paths resolve against it.
function New-EditorSession {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string]$Level,
        [Parameter(Mandatory = $true)][string]$AutomationDir,
        [string]$WorkingDirectory,
        [int]$StartTimeoutSec = 240
    )
    if (-not $WorkingDirectory) { $WorkingDirectory = Split-Path -Parent $Exe }
    New-Item -ItemType Directory -Force $AutomationDir | Out-Null

    $process = Start-Process -FilePath $Exe -WorkingDirectory $WorkingDirectory `
        -ArgumentList @('--automation', $AutomationDir, '--level', $Level) -PassThru
    $session = [pscustomobject]@{
        Process = $process
        Dir     = $AutomationDir
        Level   = $Level
        Exe     = $Exe
        Closed  = $false
    }

    # A cold start loads the level (FBX included), which can take a while; poll
    # rather than sleeping a fixed amount, and give up early if it died on the way.
    $deadline = (Get-Date).AddSeconds($StartTimeoutSec)
    $response = Join-Path $AutomationDir 'response.txt'
    [IO.File]::WriteAllLines((Join-Path $AutomationDir 'command.pending'), [string[]]@('ping'), (New-Object Text.ASCIIEncoding))
    Move-Item (Join-Path $AutomationDir 'command.pending') (Join-Path $AutomationDir 'command.txt') -Force
    while (-not (Test-Path $response)) {
        if ($process.HasExited) {
            throw "SceneEditor exited during startup (code $($process.ExitCode))." + (Get-CrashReport $session)
        }
        if ((Get-Date) -gt $deadline) {
            throw "SceneEditor did not answer ping within ${StartTimeoutSec}s (level: $Level)." + (Get-CrashReport $session)
        }
        Start-Sleep -Milliseconds 100
    }
    return $session
}

function Close-EditorSession {
    param($Session)
    if ($null -eq $Session -or $Session.Closed) { return }
    $Session.Closed = $true
    try {
        if (-not $Session.Process.HasExited) {
            Invoke-EditorCommand -Session $Session -Command 'quit' -TimeoutSec 15 | Out-Null
        }
    }
    catch {
        # A hung or already-dead editor must not mask the test results.
    }
    for ($i = 0; $i -lt 50 -and -not $Session.Process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
    }
    if (-not $Session.Process.HasExited) {
        try { $Session.Process.Kill() } catch { }
    }
}

# Splits the editor's response into one object per command:
#   Command  the echoed command line
#   Status   'OK' or 'ERR'
#   Text     the rest of the OK/ERR line
#   Payload  the lines after it, up to the next command
function ConvertTo-EditorResults {
    param([string[]]$Lines)
    $results = @()
    $current = $null
    foreach ($line in $Lines) {
        if ($line -like '# *') {
            if ($null -ne $current) { $results += $current }
            $current = [pscustomobject]@{
                Command = $line.Substring(2)
                Status  = ''
                Text    = ''
                Payload = @()
            }
        }
        elseif ($null -ne $current -and $current.Status -eq '') {
            if ($line -like 'OK*') {
                $current.Status = 'OK'
                if ($line.Length -gt 3) { $current.Text = $line.Substring(3) }
            }
            elseif ($line -like 'ERR*') {
                $current.Status = 'ERR'
                if ($line.Length -gt 4) { $current.Text = $line.Substring(4) }
            }
            else {
                $current.Payload += $line
            }
        }
        elseif ($null -ne $current) {
            $current.Payload += $line
        }
    }
    if ($null -ne $current) { $results += $current }
    return , $results
}

# Writes one batch of commands and waits for the frame that answers it.
# ASCII on purpose: Set-Content -Encoding utf8 prepends a BOM, which makes the
# first command of every batch fail to parse while the rest run.
function Invoke-EditorCommand {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string[]]$Command,
        [int]$TimeoutSec = 90
    )
    $response = Join-Path $Session.Dir 'response.txt'
    if (Test-Path $response) { Remove-Item $response -Force }
    $pending = Join-Path $Session.Dir 'command.pending'
    [IO.File]::WriteAllLines($pending, [string[]]$Command, (New-Object Text.ASCIIEncoding))
    Move-Item $pending (Join-Path $Session.Dir 'command.txt') -Force

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not (Test-Path $response)) {
        if ($Session.Process.HasExited) {
            throw "editor exited (code $($Session.Process.ExitCode)) while running: $($Command -join ' | ')" + (Get-CrashReport $Session)
        }
        if ((Get-Date) -gt $deadline) {
            throw "editor did not answer within ${TimeoutSec}s: $($Command -join ' | ')" + (Get-CrashReport $Session)
        }
        Start-Sleep -Milliseconds 40
    }
    $lines = @([IO.File]::ReadAllLines($response))
    return (ConvertTo-EditorResults -Lines $lines)
}

# Runs $Count frames without changing anything. Some engine state settles over
# frames rather than instantly (the world tick consumes camera/transform edits,
# temporal passes accumulate), and a test that reads it in the same batch reads
# the frame before.
function Step-EditorFrames {
    param($Session, [int]$Count = 1)
    for ($i = 0; $i -lt $Count; $i++) {
        Invoke-EditorCommand -Session $Session -Command 'ping' | Out-Null
    }
}

#--- typed readers ------------------------------------------------------------

function Get-State {
    param($Session)
    $r = Invoke-EditorCommand -Session $Session -Command 'state'
    Assert-Ok -Result $r[0]
    return ($r[0].Text | ConvertFrom-Json)
}

function Get-Render {
    param($Session)
    $r = Invoke-EditorCommand -Session $Session -Command 'render'
    Assert-Ok -Result $r[0]
    return ($r[0].Text | ConvertFrom-Json)
}

function Get-Camera {
    param($Session)
    $r = Invoke-EditorCommand -Session $Session -Command 'camera'
    Assert-Ok -Result $r[0]
    return ($r[0].Text | ConvertFrom-Json)
}

function Get-Component {
    param($Session, [string]$Entity, [string]$Component)
    $r = Invoke-EditorCommand -Session $Session -Command "component $Entity $Component"
    Assert-Ok -Result $r[0]
    if ($r[0].Payload.Count -lt 1) { throw "component $Entity $Component returned no JSON payload" }
    return ($r[0].Payload[0] | ConvertFrom-Json)
}

function Get-Position {
    param($Session, [string]$Entity)
    return (Get-Component -Session $Session -Entity $Entity -Component 'Transform').position
}

# The template's component blocks, as a hashtable of name -> parsed JSON.
function Get-TemplateInfo {
    param($Session, [string]$Template)
    $r = Invoke-EditorCommand -Session $Session -Command "template_info $Template"
    Assert-Ok -Result $r[0]
    $blocks = @{}
    foreach ($line in $r[0].Payload) {
        $space = $line.IndexOf(' ')
        if ($space -lt 1) { continue }
        $blocks[$line.Substring(0, $space)] = ($line.Substring($space + 1) | ConvertFrom-Json)
    }
    return $blocks
}

# The name and landing position out of a `place` response. Instance names carry a
# session-wide counter (`tf_box_inst_7`), so a test must read the name back rather
# than predict it.
function Get-PlacedInstance {
    param($Result)
    Assert-Ok -Result $Result
    if ($Result.Text -notmatch ':\s*(\S+)\s+at\s+(-?[\d.eE+]+)\s+(-?[\d.eE+]+)\s+(-?[\d.eE+]+)\s*$') {
        throw "could not read a placed instance out of: $($Result.Text)"
    }
    return [pscustomobject]@{
        Name = $Matches[1]
        X    = [double]$Matches[2]
        Y    = [double]$Matches[3]
        Z    = [double]$Matches[4]
    }
}

# Entity names from `list_entities`, in the order the editor reports them.
function Get-EntityNames {
    param($Session)
    $r = Invoke-EditorCommand -Session $Session -Command 'list_entities'
    Assert-Ok -Result $r[0]
    $names = @()
    foreach ($line in $r[0].Payload) {
        $space = $line.IndexOf(' ')
        if ($space -gt 0) { $names += $line.Substring(0, $space) } else { $names += $line }
    }
    return , $names
}

#--- assertions ---------------------------------------------------------------

function Assert-True {
    param([bool]$Condition, [string]$Message = 'condition was false')
    if (-not $Condition) { throw $Message }
}

function Assert-False {
    param([bool]$Condition, [string]$Message = 'condition was true')
    if ($Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message = '')
    if ("$Expected" -ne "$Actual") {
        throw ("{0}expected [{1}], got [{2}]" -f $(if ($Message) { "$Message`: " } else { '' }), $Expected, $Actual)
    }
}

function Assert-NotEqual {
    param($Expected, $Actual, [string]$Message = '')
    if ("$Expected" -eq "$Actual") {
        throw ("{0}expected a value other than [{1}]" -f $(if ($Message) { "$Message`: " } else { '' }), $Expected)
    }
}

function Assert-Near {
    param([double]$Expected, [double]$Actual, [double]$Tolerance = 0.001, [string]$Message = '')
    $delta = [Math]::Abs($Expected - $Actual)
    if ($delta -gt $Tolerance) {
        throw ("{0}expected {1} +/- {2}, got {3} (off by {4})" -f $(if ($Message) { "$Message`: " } else { '' }), $Expected, $Tolerance, $Actual, $delta)
    }
}

function Assert-Vector3Near {
    param($Expected, $Actual, [double]$Tolerance = 0.001, [string]$Message = 'vector')
    Assert-Near -Expected $Expected.x -Actual $Actual.x -Tolerance $Tolerance -Message "$Message.x"
    Assert-Near -Expected $Expected.y -Actual $Actual.y -Tolerance $Tolerance -Message "$Message.y"
    Assert-Near -Expected $Expected.z -Actual $Actual.z -Tolerance $Tolerance -Message "$Message.z"
}

function Assert-Match {
    param([string]$Pattern, [string]$Actual, [string]$Message = '')
    if ($Actual -notmatch $Pattern) {
        throw ("{0}expected a match for /{1}/, got [{2}]" -f $(if ($Message) { "$Message`: " } else { '' }), $Pattern, $Actual)
    }
}

function Assert-NotMatch {
    param([string]$Pattern, [string]$Actual, [string]$Message = '')
    if ($Actual -match $Pattern) {
        throw ("{0}expected no match for /{1}/, got [{2}]" -f $(if ($Message) { "$Message`: " } else { '' }), $Pattern, $Actual)
    }
}

function Assert-Contains {
    param([string[]]$Collection, [string]$Value, [string]$Message = 'collection')
    if ($Collection -notcontains $Value) {
        throw ("{0} does not contain [{1}] (has: {2})" -f $Message, $Value, ($Collection -join ', '))
    }
}

function Assert-NotContains {
    param([string[]]$Collection, [string]$Value, [string]$Message = 'collection')
    if ($Collection -contains $Value) {
        throw ("{0} unexpectedly contains [{1}]" -f $Message, $Value)
    }
}

# Every result in a batch answered OK. Passing the whole batch is the normal use:
# it names the command that failed rather than the index.
function Assert-Ok {
    param(
        [Parameter(ValueFromPipeline = $true)]$Result,
        [string]$Message = ''
    )
    process {
        foreach ($r in @($Result)) {
            if ($r.Status -ne 'OK') {
                throw ("{0}`"{1}`" answered {2} {3}" -f $(if ($Message) { "$Message`: " } else { '' }), $r.Command, $r.Status, $r.Text)
            }
        }
    }
}

# The command was rejected - and rejected for the stated reason, so a test does
# not pass on some unrelated failure.
function Assert-Err {
    param($Result, [string]$Pattern = '', [string]$Message = '')
    foreach ($r in @($Result)) {
        if ($r.Status -ne 'ERR') {
            throw ("{0}`"{1}`" was expected to fail, answered {2} {3}" -f $(if ($Message) { "$Message`: " } else { '' }), $r.Command, $r.Status, $r.Text)
        }
        if ($Pattern -and $r.Text -notmatch $Pattern) {
            throw ("{0}`"{1}`" failed with [{2}], expected /{3}/" -f $(if ($Message) { "$Message`: " } else { '' }), $r.Command, $r.Text, $Pattern)
        }
    }
}

function Assert-FileExists {
    param([string]$Path, [string]$Message = 'file')
    if (-not (Test-Path $Path)) { throw "$Message does not exist: $Path" }
}

function Assert-FileNotExists {
    param([string]$Path, [string]$Message = 'file')
    if (Test-Path $Path) { throw "$Message unexpectedly exists: $Path" }
}

#--- screenshots --------------------------------------------------------------

Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue

# Mean RGB and the share of non-black pixels over a rectangle given in fractions
# of the image, so a test is independent of the window size. The default rect is
# the middle of the viewport: the editor's panels occupy the left and right edges
# and would otherwise dominate every statistic.
function Get-ImageStats {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [double]$Left = 0.35, [double]$Top = 0.35,
        [double]$Right = 0.65, [double]$Bottom = 0.65,
        [int]$Step = 4
    )
    Assert-FileExists -Path $Path -Message 'screenshot'
    $bmp = New-Object System.Drawing.Bitmap($Path)
    try {
        $x0 = [int]($bmp.Width * $Left); $x1 = [int]($bmp.Width * $Right)
        $y0 = [int]($bmp.Height * $Top); $y1 = [int]($bmp.Height * $Bottom)
        $sumR = 0.0; $sumG = 0.0; $sumB = 0.0
        $count = 0; $lit = 0
        for ($y = $y0; $y -lt $y1; $y += $Step) {
            for ($x = $x0; $x -lt $x1; $x += $Step) {
                $p = $bmp.GetPixel($x, $y)
                $sumR += $p.R; $sumG += $p.G; $sumB += $p.B
                if ($p.R -gt 8 -or $p.G -gt 8 -or $p.B -gt 8) { $lit++ }
                $count++
            }
        }
        if ($count -eq 0) { throw "empty sample rectangle in $Path" }
        return [pscustomobject]@{
            Width    = $bmp.Width
            Height   = $bmp.Height
            Samples  = $count
            MeanR    = $sumR / $count
            MeanG    = $sumG / $count
            MeanB    = $sumB / $count
            Mean     = ($sumR + $sumG + $sumB) / (3.0 * $count)
            LitShare = $lit / [double]$count
        }
    }
    finally {
        $bmp.Dispose()
    }
}

# Share of sampled pixels that differ by more than $Threshold in any channel.
# The measure for a screenshot A/B: the engine accumulates temporally, so the
# comparison is against a stochastic floor rather than against zero.
function Get-ImageDifference {
    param(
        [Parameter(Mandatory = $true)][string]$PathA,
        [Parameter(Mandatory = $true)][string]$PathB,
        [double]$Left = 0.15, [double]$Top = 0.05,
        [double]$Right = 0.85, [double]$Bottom = 0.95,
        [int]$Step = 4, [int]$Threshold = 8
    )
    Assert-FileExists -Path $PathA -Message 'screenshot A'
    Assert-FileExists -Path $PathB -Message 'screenshot B'
    $a = New-Object System.Drawing.Bitmap($PathA)
    $b = New-Object System.Drawing.Bitmap($PathB)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
            throw "screenshot sizes differ: $($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)"
        }
        $x0 = [int]($a.Width * $Left); $x1 = [int]($a.Width * $Right)
        $y0 = [int]($a.Height * $Top); $y1 = [int]($a.Height * $Bottom)
        $count = 0; $differing = 0; $sum = 0.0; $max = 0
        for ($y = $y0; $y -lt $y1; $y += $Step) {
            for ($x = $x0; $x -lt $x1; $x += $Step) {
                $pa = $a.GetPixel($x, $y); $pb = $b.GetPixel($x, $y)
                $d = [Math]::Max([Math]::Abs($pa.R - $pb.R), [Math]::Max([Math]::Abs($pa.G - $pb.G), [Math]::Abs($pa.B - $pb.B)))
                if ($d -gt $Threshold) { $differing++ }
                if ($d -gt $max) { $max = $d }
                $sum += $d
                $count++
            }
        }
        return [pscustomobject]@{
            Samples        = $count
            DifferingShare = $differing / [double]$count
            MeanDelta      = $sum / $count
            MaxDelta       = $max
        }
    }
    finally {
        $a.Dispose(); $b.Dispose()
    }
}

Export-ModuleMember -Function *
