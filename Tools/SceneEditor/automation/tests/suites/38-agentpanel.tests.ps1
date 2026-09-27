# fixture: empty
# description: The Claude panel - the agent process, its own automation channel, the transcript, one undo step per turn, stop, crash and resume. Runs against assets\fake-claude.js, a scripted stand-in that speaks Claude Code's stream-json protocol and edits the level through the agent's channel exactly as the real MCP server does; no network, no account.

$fake = Join-Path $PSScriptRoot '..\assets\fake-claude.js' | Resolve-Path | Select-Object -ExpandProperty Path
$starts = Join-Path $Session.Dir 'fake-claude-starts.jsonl'

function Get-AgentStatus {
    $r = Send 'agent_status'
    Assert-Ok $r[0]
    $s = @{}
    foreach ($pair in ($r[0].Text -split ' ')) {
        $kv = $pair -split '=', 2
        if ($kv.Count -eq 2) { $s[$kv[0]] = $kv[1] }
    }
    foreach ($line in $r[0].Payload) {
        $kv = $line -split '=', 2
        if ($kv.Count -eq 2) { $s[$kv[0]] = $kv[1] }
    }
    return $s
}

# Turns run on the fake's own clock; the editor only learns about them between
# frames, so poll.
function Wait-Agent([string]$Status = 'ready', [int]$TimeoutSec = 30) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $s = Get-AgentStatus
        if ($s.status -eq $Status) { return $s }
        Start-Sleep -Milliseconds 100
    }
    throw "agent never reached '$Status' (last: $($s.status) $($s.error))"
}

function Get-Transcript {
    $r = Send 'agent_transcript'
    Assert-Ok $r[0]
    return @($r[0].Payload)
}

function Get-LastStart {
    Assert-FileExists $starts
    return (Get-Content $starts | Select-Object -Last 1 | ConvertFrom-Json)
}

function Get-ArgAfter($Start, [string]$Name) {
    $i = [array]::IndexOf([string[]]$Start.argv, $Name)
    if ($i -lt 0) { return $null }
    return $Start.argv[$i + 1]
}

Test 'the panel is pointed at the scripted Claude' {
    # A model remembered from the person's own sessions would reach the fake too.
    SendOk 'agent_model default' | Out-Null
    $r = SendOk "agent_claude ""$fake"""
    Assert-Match 'fake-claude\.js' $r[0].Text
    $setup = SendOk 'agent_setup'
    Assert-True (($setup[0].Payload -join "`n") -match 'server=.*mcp\\server\.js') 'the MCP server was not found'
    Assert-Equal 'idle' (Get-AgentStatus).status
}

Test 'View/Claude and agent_show open the panel' {
    SendOk 'agent_show' | Out-Null
    $menus = (Send 'menus')[0].Payload -join "`n"
    Assert-Match 'View/Claude' $menus
}

Test 'a message starts the process and the reply lands in the transcript' {
    SendOk 'agent_send hello there' | Out-Null
    $s = Wait-Agent
    Assert-Match '^fake-' $s.session
    Assert-Equal 'fake-model' $s.model
    Assert-Equal '0' $s.group_open
    $t = Get-Transcript
    Assert-Contains $t 'user hello there'
    Assert-Contains $t 'assistant echo: hello there'
    Assert-True (@($t | Where-Object { $_ -like 'result done*' }).Count -eq 1) "no result line in: $($t -join ' | ')"
}

Test 'the launch is locked down to the editor tools' {
    $start = Get-LastStart
    $argv = [string[]]$start.argv
    Assert-Contains $argv '--strict-mcp-config'
    Assert-Match 'Write' (Get-ArgAfter $start '--disallowedTools')
    Assert-Match 'Bash' (Get-ArgAfter $start '--disallowedTools')
    Assert-Match '^mcp__hotbite-editor ' (Get-ArgAfter $start '--allowedTools')
    Assert-Equal 'none' (Get-ArgAfter $start '--permission-prompts')
    $mcp = Get-Content (Get-ArgAfter $start '--mcp-config') -Raw | ConvertFrom-Json
    $serverArgs = [string[]]$mcp.mcpServers.'hotbite-editor'.args
    Assert-Contains $serverArgs '--embedded'
    # Its own channel, never the one this suite drives.
    $dir = $serverArgs[[array]::IndexOf($serverArgs, '--dir') + 1]
    Assert-Equal (Join-Path $Session.Dir 'agent') $dir
    Assert-Equal ([IO.Path]::GetFullPath($Project).TrimEnd('\')) ([IO.Path]::GetFullPath($start.cwd).TrimEnd('\'))
    Assert-FileExists (Get-ArgAfter $start '--append-system-prompt-file')
}

Test 'everything one turn does is one undo step' {
    $a = Get-Position -Session $Session -Entity 'box_a'
    $b = Get-Position -Session $Session -Entity 'box_b'
    SendOk 'agent_send run: select box_a ; set_position 5 0 0 ; select box_b ; set_position 0 0 7' | Out-Null
    $s = Wait-Agent
    Assert-Vector3Near -Expected @{ x = 5; y = 0; z = 0 } -Actual (Get-Position -Session $Session -Entity 'box_a')
    Assert-Vector3Near -Expected @{ x = 0; y = 0; z = 7 } -Actual (Get-Position -Session $Session -Entity 'box_b')
    Assert-Match '^Claude: run: select box_a' $s.undo_top
    Assert-Match '\(2 edits\)$' $s.undo_top

    SendOk 'undo' | Out-Null
    Assert-Vector3Near -Expected $a -Actual (Get-Position -Session $Session -Entity 'box_a')
    Assert-Vector3Near -Expected $b -Actual (Get-Position -Session $Session -Entity 'box_b')
    SendOk 'redo' | Out-Null
    Assert-Vector3Near -Expected @{ x = 5; y = 0; z = 0 } -Actual (Get-Position -Session $Session -Entity 'box_a')
    Assert-Vector3Near -Expected @{ x = 0; y = 0; z = 7 } -Actual (Get-Position -Session $Session -Entity 'box_b')
    SendOk 'undo' | Out-Null

    $t = Get-Transcript
    Assert-True (@($t | Where-Object { $_ -like 'tool editor_command ok*' }).Count -ge 1) "no successful tool call in: $($t -join ' | ')"
}

Test 'a turn that changes nothing leaves the history alone' {
    SendOk 'select box_c', 'set_position 0 3 3' | Out-Null
    $before = (Get-AgentStatus).undo_top
    SendOk 'agent_send just talking' | Out-Null
    $s = Wait-Agent
    Assert-Equal $before $s.undo_top
    SendOk 'undo' | Out-Null
}

Test 'a failing command is a failed tool call, not a failed turn' {
    SendOk 'agent_send run: select no_such_entity' | Out-Null
    Wait-Agent | Out-Null
    $t = Get-Transcript
    Assert-True (@($t | Where-Object { $_ -like 'tool editor_command error*' }).Count -eq 1) "no failed tool call in: $($t -join ' | ')"
    Assert-Contains $t 'assistant ran 1 command(s) with errors'
}

Test 'a second message while busy is refused, and Stop ends the turn' {
    SendOk 'agent_send slow' | Out-Null
    Wait-Agent -Status 'busy' | Out-Null
    $r = Send 'agent_send too soon'
    Assert-Err $r[0]
    SendOk 'agent_stop' | Out-Null
    $s = Wait-Agent
    Assert-Equal '0' $s.group_open
    $t = Get-Transcript
    Assert-Equal 'result stopped in 0.0 s' $t[-1]
}

Test 'a crash mid-turn is reported, and the next message resumes the conversation' {
    $sid = (Get-AgentStatus).session
    SendOk 'agent_send crash' | Out-Null
    $s = Wait-Agent -Status 'failed'
    Assert-Match 'code 3' $s.error
    Assert-Match 'crashing on purpose' $s.error
    Assert-Equal '0' $s.group_open

    SendOk 'agent_send back again' | Out-Null
    $s = Wait-Agent
    Assert-Equal $sid $s.session
    Assert-Equal $sid (Get-ArgAfter (Get-LastStart) '--resume')
    Assert-Contains (Get-Transcript) 'assistant echo: back again'
}

Test 'changing the model restarts the process with it, keeping the conversation' {
    $sid = (Get-AgentStatus).session
    $count = @(Get-Content $starts).Count
    SendOk 'agent_model sonnet' | Out-Null
    SendOk 'agent_send which model' | Out-Null
    $s = Wait-Agent
    Assert-Equal 'sonnet' $s.model
    Assert-Equal ($count + 1) @(Get-Content $starts).Count
    Assert-Equal 'sonnet' (Get-ArgAfter (Get-LastStart) '--model')
    Assert-Equal $sid (Get-ArgAfter (Get-LastStart) '--resume')
    SendOk 'agent_model default' | Out-Null
}

Test 'the cost is summed across turns and restarts' {
    $s = Get-AgentStatus
    Assert-True ([double]$s.cost -gt 0.02) "cost $($s.cost)"
}

Test 'New chat forgets the conversation' {
    SendOk 'agent_new' | Out-Null
    $s = Get-AgentStatus
    Assert-Equal 'idle' $s.status
    Assert-Equal '-' $s.session
    Assert-Equal '0' $s.items
    SendOk 'agent_send fresh start' | Out-Null
    $s = Wait-Agent
    Assert-True ($null -eq (Get-ArgAfter (Get-LastStart) '--resume')) 'a new chat still resumed the old session'
    Assert-Equal '0.0100' $s.cost
}

Test 'the suite channel keeps working while the agent uses its own' {
    SendOk 'agent_send slow' | Out-Null
    Wait-Agent -Status 'busy' | Out-Null
    SendOk 'select box_a', 'set_position 1 1 1' | Out-Null
    Assert-Vector3Near -Expected @{ x = 1; y = 1; z = 1 } -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'agent_stop' | Out-Null
    Wait-Agent | Out-Null
    # The edit landed inside the turn's group - a user edit during a turn joins it.
    Assert-Match '^Claude: slow' (Get-AgentStatus).undo_top
    SendOk 'undo' | Out-Null
}
