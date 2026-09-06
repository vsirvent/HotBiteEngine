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

Test 'render settings survive a save and reload, but the debug-only ones never do' {
    # Dial every persistent toggle away from ApplyHighDefaults, including a manual
    # DOF focus (autofocus off, so dof_focus/dof_amplitude are actually exercised),
    # and also turn on a debug buffer view and a denoiser bypass - RenderSettings
    # deliberately never saves those (RenderSettings.h: "Debug views are a tool,
    # never a level's state"), so they must reset to the defaults on the next open.
    SendOk 'render rt_quality low' | Out-Null
    SendOk 'render rt_reflections 0' | Out-Null
    SendOk 'render rt_refractions 0' | Out-Null
    SendOk 'render rt_indirect 0' | Out-Null
    SendOk 'render aa 0' | Out-Null
    SendOk 'render motion_blur 0' | Out-Null
    SendOk 'render dof 1' | Out-Null
    SendOk 'render dof_autofocus 0' | Out-Null
    SendOk 'render dof_focus 42' | Out-Null
    SendOk 'render dof_amplitude 7.5' | Out-Null
    SendOk 'render lens_flare 0' | Out-Null
    SendOk 'render lens 0' | Out-Null
    SendOk 'render lens_aberration 0.3' | Out-Null
    SendOk 'render lens_grain 0.4' | Out-Null
    SendOk 'render lens_vignette 0.5' | Out-Null
    SendOk 'render wireframe 1' | Out-Null
    SendOk 'render debug_buffer motion' | Out-Null
    SendOk 'render gi_denoise 0' | Out-Null

    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $render = $level.editor.render
    Assert-True -Condition ($null -ne $render) -Message 'the editor/render block is written'
    Assert-Equal -Expected 'low' -Actual $render.rt_quality
    Assert-Equal -Expected 'False' -Actual $render.rt_reflections
    Assert-Equal -Expected 'False' -Actual $render.rt_refractions
    Assert-Equal -Expected 'False' -Actual $render.rt_indirect
    Assert-Equal -Expected 'False' -Actual $render.aa
    Assert-Equal -Expected 'False' -Actual $render.motion_blur
    Assert-Equal -Expected 'True' -Actual $render.dof
    Assert-Equal -Expected 'False' -Actual $render.dof_autofocus
    Assert-Near -Expected 42.0 -Actual $render.dof_focus -Tolerance 0.01
    Assert-Near -Expected 7.5 -Actual $render.dof_amplitude -Tolerance 0.01
    Assert-Equal -Expected 'False' -Actual $render.lens_flare
    Assert-Equal -Expected 'False' -Actual $render.lens
    Assert-Near -Expected 0.3 -Actual $render.lens_aberration -Tolerance 0.001
    Assert-Near -Expected 0.4 -Actual $render.lens_grain -Tolerance 0.001
    Assert-Near -Expected 0.5 -Actual $render.lens_vignette -Tolerance 0.001
    Assert-Equal -Expected 'True' -Actual $render.wireframe
    $savedKeys = @($render.PSObject.Properties.Name)
    foreach ($debugKey in @('debug_buffer', 'debug_gain', 'gi_denoise', 'rt_denoise')) {
        Assert-NotContains -Collection $savedKeys -Value $debugKey `
            -Message 'a debug-only setting is never part of the saved level'
    }

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-render'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Get-Render -Session $reloaded
        Assert-Equal -Expected 'low' -Actual $r.rt_quality -Message 'reloaded'
        Assert-Equal -Expected 'False' -Actual $r.rt_reflections
        Assert-Equal -Expected 'False' -Actual $r.rt_refractions
        Assert-Equal -Expected 'False' -Actual $r.rt_indirect
        Assert-Equal -Expected 'False' -Actual $r.aa
        Assert-Equal -Expected 'False' -Actual $r.motion_blur
        Assert-Equal -Expected 'True' -Actual $r.dof
        Assert-Equal -Expected 'False' -Actual $r.dof_autofocus
        Assert-Near -Expected 42.0 -Actual $r.dof_focus -Tolerance 0.01
        Assert-Near -Expected 7.5 -Actual $r.dof_amplitude -Tolerance 0.01
        Assert-Equal -Expected 'False' -Actual $r.lens_flare
        Assert-Equal -Expected 'False' -Actual $r.lens
        Assert-Near -Expected 0.3 -Actual $r.lens_aberration -Tolerance 0.001
        Assert-Near -Expected 0.4 -Actual $r.lens_grain -Tolerance 0.001
        Assert-Near -Expected 0.5 -Actual $r.lens_vignette -Tolerance 0.001
        Assert-Equal -Expected 'True' -Actual $r.wireframe

        # Debug tooling always comes back at ApplyHighDefaults, never at what the
        # previous session happened to be showing.
        Assert-Equal -Expected 'off' -Actual $r.debug_buffer -Message 'debug buffer resets on load'
        Assert-Equal -Expected 'True' -Actual $r.gi_denoise -Message 'denoiser bypass resets on load'
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
