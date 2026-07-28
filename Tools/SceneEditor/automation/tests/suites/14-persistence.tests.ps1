# fixture: empty
# description: Saving and reloading - what the level file records, and that a second editor rebuilds the same scene.

Test 'a saved level keeps the sections World::Load reads' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    foreach ($section in @('path', 'lights', 'templates', 'instances', 'entities',
                           'models', 'material_files', 'clones', 'removed_entities')) {
        Assert-True -Condition ($null -ne $level.world.$section) -Message "world.$section is written"
    }
    Assert-True -Condition ($null -ne $level.editor) -Message 'the editor block is written'
}

Test 'a save records placements, renames, deletions and groups' {
    $placed = Get-PlacedInstance -Result (Send 'place tf_box')[0]
    SendOk "rename $($placed.Name) saved_box" | Out-Null
    SendOk 'select saved_box' | Out-Null
    SendOk 'set_position 7 1 -2' | Out-Null
    SendOk 'create_group Saved', 'set_group saved_box Saved' | Out-Null
    SendOk 'select box_c', 'delete' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $record = $level.world.instances | Where-Object { $_.name -eq 'saved_box' }
    Assert-True -Condition ($null -ne $record) -Message 'the placed-and-renamed instance is in the file'
    Assert-Near -Expected 7.0 -Actual $record.position.x -Tolerance 0.001
    Assert-Equal -Expected 'tf_box' -Actual $record.template
    Assert-Contains -Collection @($level.editor.groups.Saved) -Value 'saved_box' -Message 'saved group'
    $names = @($level.world.instances | ForEach-Object { $_.name })
    Assert-NotContains -Collection $names -Value 'box_c' -Message 'the deleted instance is gone'
}

Test 'a reloaded level rebuilds the same scene' {
    # One editor loads one level per session, so this is a second process on the
    # file the first one just wrote - the check that a save is actually loadable.
    $before = @(Get-EntityNames -Session $Session | Sort-Object)
    $beforePos = Get-Position -Session $Session -Entity 'saved_box'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-channel'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $after = @(Get-EntityNames -Session $reloaded | Sort-Object)
        Assert-Equal -Expected ($before -join ',') -Actual ($after -join ',') -Message 'the same entities came back'

        $afterPos = Get-Position -Session $reloaded -Entity 'saved_box'
        Assert-Vector3Near -Expected $beforePos -Actual $afterPos -Tolerance 0.001 `
            -Message 'and at the same place'

        $groups = (Invoke-EditorCommand -Session $reloaded -Command 'list_groups')[0]
        Assert-Match -Pattern 'saved_box' -Actual ($groups.Payload -join "`n") -Message 'groups were restored'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a component edit survives the round trip' {
    SendOk "set_component saved_box Base ""{'pass':2,'cast_shadow':false}""" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-components'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $base = Get-Component -Session $reloaded -Entity 'saved_box' -Component 'Base'
        Assert-Equal -Expected 2 -Actual $base.pass -Message 'the per-entity delta reloaded'
        Assert-Equal -Expected 'False' -Actual $base.cast_shadow
        # ...and its siblings, which never carried the delta, did not pick it up.
        $sibling = Get-Component -Session $reloaded -Entity 'box_a' -Component 'Base'
        Assert-Equal -Expected 1 -Actual $sibling.pass -Message 'the edit was per entity'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a template stored in a file reloads through its .tpl' {
    SendOk 'create_template pers_widget' | Out-Null
    SendOk "template_set pers_widget Transform ""{'scale':{'x':3.0,'y':3.0,'z':3.0}}""" | Out-Null
    SendOk 'template_storage pers_widget file' | Out-Null
    SendOk 'place pers_widget' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Templates\pers_widget.tpl') -Message 'the .tpl'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-templates'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $blocks = Get-TemplateInfo -Session $reloaded -Template 'pers_widget'
        Assert-Near -Expected 3.0 -Actual $blocks['Transform'].scale.x -Tolerance 0.001
        $r = Invoke-EditorCommand -Session $reloaded -Command 'list_templates'
        $line = $r[0].Payload | Where-Object { $_ -like 'pers_widget *' }
        Assert-Match -Pattern 'in=file' -Actual $line
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'an inline template reloads out of the level itself' {
    SendOk 'create_template pers_inline' | Out-Null
    SendOk 'template_storage pers_inline level' | Out-Null
    SendOk "template_set pers_inline Transform ""{'position':{'x':1.0,'y':2.0,'z':3.0}}""" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    Assert-FileNotExists -Path (Join-Path $Assets 'Templates\pers_inline.tpl') -Message 'no file is left behind'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-inline'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $blocks = Get-TemplateInfo -Session $reloaded -Template 'pers_inline'
        Assert-Near -Expected 2.0 -Actual $blocks['Transform'].position.y -Tolerance 0.001
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a material edit reloads out of its .mat file' {
    SendOk 'create_material PersistBlue materials\test.mat' | Out-Null
    SendOk 'set_material box_a PersistBlue' | Out-Null
    SendOk 'save_materials', 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-materials'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Invoke-EditorCommand -Session $reloaded -Command 'materials'
        $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $names -Value 'PersistBlue' -Message 'reloaded materials'
        Assert-Equal -Expected 'PersistBlue' `
            -Actual (Get-Component -Session $reloaded -Entity 'box_a' -Component 'Material').name `
            -Message 'and the assignment came with it'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a composed template reloads with its parts' {
    SendOk 'create_template pers_rig' | Out-Null
    SendOk 'template_add_part pers_rig tf_marker' | Out-Null
    SendOk "template_set_part pers_rig tf_marker ""{'position':{'x':2.0,'y':0.0,'z':0.0}}""" | Out-Null
    $placed = Get-PlacedInstance -Result (Send 'place pers_rig')[0]
    SendOk "rename $($placed.Name) pers_composed" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-parts'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $names = Get-EntityNames -Session $reloaded
        Assert-Contains -Collection $names -Value 'pers_composed' -Message 'the root reloaded'
        Assert-Contains -Collection $names -Value 'pers_composed__tf_marker' -Message 'and the part with it'
        Assert-Vector3Near -Expected @{ x = 2.0; y = 0.0; z = 0.0 } `
            -Actual (Get-Position -Session $reloaded -Entity 'pers_composed__tf_marker') -Tolerance 0.01
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a cut entity is genuinely gone after save and reload' {
    SendOk 'cut box_b' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-cut'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        Assert-NotContains -Collection (Get-EntityNames -Session $reloaded) -Value 'box_b' -Message 'reloaded scene'
        Assert-Err -Result (Invoke-EditorCommand -Session $reloaded -Command 'paste')[0] `
            -Message 'and the clipboard did not survive the session either'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
