# fixture: empty
# description: Add/<kind> - entities that arrive with the components their kind typically carries, as one undo step.

Test 'every preset is in the menu registry' {
    $r = SendOk 'menus'
    $paths = @($r[0].Payload | ForEach-Object { ($_ -replace ' \(disabled\)$', '') })
    foreach ($p in @('Add/Sky', 'Add/Directional Light', 'Add/Point Light', 'Add/Spot Light',
                     'Add/Ambient Light', 'Add/Mesh Object', 'Add/Physics Object', 'Add/Gaussian Splat')) {
        Assert-Contains -Collection $paths -Value $p -Message 'menu registry'
    }
}

Test 'Add/Mesh Object gives an entity that can be drawn' {
    SendOk 'menu "Add/Mesh Object"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    Assert-Equal -Expected 'Mesh' -Actual $name
    $components = (SendOk "components $name")[0].Payload
    foreach ($c in @('Base', 'Transform', 'Mesh', 'Material', 'Bounds')) {
        Assert-Contains -Collection $components -Value $c -Message 'mesh object components'
    }
}

Test 'Add/Sky carries the sky and its sun' {
    SendOk 'menu "Add/Sky"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    $components = (SendOk "components $name")[0].Payload
    foreach ($c in @('Sky', 'DirectionalLight', 'AmbientLight')) {
        Assert-Contains -Collection $components -Value $c -Message 'sky components'
    }
}

Test 'Add/Spot Light is a point light with the cone on' {
    SendOk 'menu "Add/Spot Light"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    $light = Get-Component -Session $Session -Entity $name -Component 'PointLight'
    Assert-Equal -Expected $true -Actual $light.spot
}

Test 'Add/Gaussian Splat carries a splat cloud' {
    SendOk 'menu "Add/Gaussian Splat"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    Assert-Contains -Collection (SendOk "components $name")[0].Payload -Value 'SplatCloud'
}

Test 'a preset is one undo step, and redo restores its components' {
    SendOk 'menu "Add/Point Light"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    SendOk 'undo' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value $name -Message 'after one undo'
    SendOk 'redo' | Out-Null
    Assert-Contains -Collection (SendOk "components $name")[0].Payload -Value 'PointLight' -Message 'after redo'
}

Test 'presets survive a save and reload' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-presets'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $names = Get-EntityNames -Session $reloaded
        Assert-Contains -Collection $names -Value 'Mesh' -Message 'reloaded scene'
        $c = (Invoke-EditorCommand -Session $reloaded -Command 'components Mesh')[0].Payload
        Assert-Contains -Collection $c -Value 'Material' -Message 'reloaded components'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
