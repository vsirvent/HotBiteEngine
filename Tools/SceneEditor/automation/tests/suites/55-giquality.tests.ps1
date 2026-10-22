# fixture: empty
# description: The indirect light's quality, which is the resolution of its texture: low (a third of the screen's size on each side, what it always was), medium (a half) or high (every pixel). A per-level option stored with the render settings, in the Render menu's "GI quality" and `render gi_quality`.

Test 'the render dump has the GI quality, low in a level that does not ask for it' {
    $r = Get-Render -Session $Session
    Assert-True -Condition ($null -ne $r.gi_quality) -Message 'the dump has gi_quality'
    Assert-Equal -Expected 'low' -Actual $r.gi_quality -Message 'low by default'
}

Test 'the setting takes the three qualities and nothing else, while the editor runs' {
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gi_quality ultra')[0] -Message 'not a quality'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render gi_quality')[0] -Message 'no value'
    foreach ($q in 'medium', 'high', 'low') {
        SendOk "render gi_quality $q" | Out-Null
        Assert-Equal -Expected $q -Actual (Get-Render -Session $Session).gi_quality -Message "set to $q"
        # The editor goes on drawing with the textures at their new size.
        SendOk 'ping' | Out-Null
    }
}

Test 'the quality is stored in the level and comes back' {
    SendOk 'render gi_quality high' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    Assert-Equal -Expected 'high' -Actual $level.editor.render.gi_quality -Message "the level's render block"
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-gi-quality'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        Assert-Equal -Expected 'high' -Actual (Get-Render -Session $reloaded).gi_quality -Message 'reloaded'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
    SendOk 'render gi_quality low' | Out-Null
}
