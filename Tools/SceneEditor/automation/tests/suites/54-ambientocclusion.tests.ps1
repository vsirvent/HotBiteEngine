# fixture: empty
# description: Ambient occlusion (GTAO) in the editor: the engine's ground truth occlusion, traced against the Hi-Z pyramid of the depth map, a direction of each pixel a frame and averaged over the frames with the ray traced shadows' blend. It is a per-level option stored with the render settings, off in a level that does not ask for it; the Render menu and `render gtao ...` set it.

function Wait-Gtao {
    param([bool]$Active, [string]$What)
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $r = Get-Render -Session $Session
        if ([bool]$r.gtao_active -eq $Active) { return $r }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "gtao_active never became $Active ($What)"
}

Test 'the render dump has the ambient occlusion, off in a level that does not ask for it' {
    $r = Get-Render -Session $Session
    foreach ($key in 'gtao', 'gtao_radius', 'gtao_strength', 'gtao_direct', 'gtao_active') {
        Assert-True -Condition ($null -ne $r.$key) -Message "the dump has $key"
    }
    Assert-Equal -Expected 'False' -Actual $r.gtao -Message 'off by default'
    Assert-Equal -Expected 'False' -Actual $r.gtao_active -Message 'and so not traced'
    Assert-Near -Expected 1.0 -Actual $r.gtao_strength -Tolerance 0.001 -Message 'the whole occlusion by default'
    Assert-Near -Expected 0.0 -Actual $r.gtao_direct -Tolerance 0.001 -Message 'and only the ambient light'
}

Test 'the settings take only what makes sense' {
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gtao maybe')[0] -Message 'not a switch'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gtao_radius 0')[0] -Message 'no reach at all'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gtao_strength 2')[0] -Message 'more than all of it'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gtao_direct -1')[0] -Message 'less than none'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gtao_radius far')[0] -Message 'not a number'
}

Test 'the switch turns the occlusion on and off while the editor runs' {
    SendOk 'render gtao 1' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-Render -Session $Session).gtao
    Wait-Gtao -Active $true -What 'on' | Out-Null
    SendOk 'render gtao 0' | Out-Null
    Wait-Gtao -Active $false -What 'off' | Out-Null
}

Test 'the options are stored in the level and come back' {
    SendOk 'render gtao 1', 'render gtao_radius 2.5', 'render gtao_strength 0.7', 'render gtao_direct 0.3' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $render = $level.editor.render
    foreach ($key in 'gtao', 'gtao_radius', 'gtao_strength', 'gtao_direct') {
        Assert-True -Condition ($null -ne $render.$key) -Message "the level's render block says $key"
    }
    Assert-Near -Expected 2.5 -Actual $render.gtao_radius -Tolerance 0.001
    Assert-Near -Expected 0.7 -Actual $render.gtao_strength -Tolerance 0.001
    Assert-Near -Expected 0.3 -Actual $render.gtao_direct -Tolerance 0.001
    Assert-NotContains -Collection @($render.PSObject.Properties.Name) -Value 'gtao_active' -Message 'whether it was traced is never saved'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-gtao'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Get-Render -Session $reloaded
        Assert-Equal -Expected 'True' -Actual $r.gtao -Message 'reloaded: the switch'
        Assert-Near -Expected 2.5 -Actual $r.gtao_radius -Tolerance 0.001 -Message 'the reach'
        Assert-Near -Expected 0.7 -Actual $r.gtao_strength -Tolerance 0.001 -Message 'the strength'
        Assert-Near -Expected 0.3 -Actual $r.gtao_direct -Tolerance 0.001 -Message 'the share of the direct light'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
    SendOk 'render gtao 0', 'render gtao_radius 1', 'render gtao_strength 1', 'render gtao_direct 0' | Out-Null
}
