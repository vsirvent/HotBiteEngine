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

Test 'a single File/Save Level catches every kind of edit at once' {
    # Every test above covers one kind of edit at a time. Bug reports about saving
    # look nothing like that - they are "I changed several different things and
    # some of them were gone when I came back" - because each kind of edit lives
    # in its own dirty-tracking bucket (component_deltas, dirty_templates,
    # dirty_material_files, placed_instances, entity_groups, the grid fields...)
    # and it is only the *union* of every bucket File/Save Level actually flushes
    # that matters. A material or multi-material edit sitting in
    # dirty_material_files with nothing to flush it is exactly the bug this test
    # is named after - see SceneSerializer::Save's material-flushing block.
    #
    # So: touch one of each major kind in a single session, save the level exactly
    # once (never save_materials or save_templates directly), and reload in a
    # fresh process - the only real proof a save is both loadable and complete.

    # An entity built from nothing (created_entities).
    SendOk 'menu "Add/Entity"' | Out-Null
    $created = (Get-State -Session $Session).selected_entity_name
    SendOk "rename $created kitchen_sink_entity" | Out-Null
    SendOk 'set_position 1 2 3' | Out-Null

    # An existing FBX/JSON-authored entity, moved and renamed (the "entities"
    # override array, keyed by authored name).
    SendOk 'select box_a', 'set_position 9 0 0' | Out-Null
    SendOk 'rename box_a kitchen_sink_box' | Out-Null

    # A placed instance, moved.
    $placed = Get-PlacedInstance -Result (Send 'place tf_box')[0]
    $kitchenInstance = $placed.Name
    SendOk "select $kitchenInstance", 'set_position -5 0 -5' | Out-Null

    # A file-backed template, edited (dirty_templates).
    SendOk 'create_template kitchen_sink_template' | Out-Null
    SendOk 'template_storage kitchen_sink_template file' | Out-Null
    SendOk "template_set kitchen_sink_template Transform ""{'scale':{'x':2.0,'y':2.0,'z':2.0}}""" | Out-Null

    # A material (dirty_material_files) wearing a multi-material stack with an
    # edited layer (the same file's dirty_material_files entry, from a different
    # editor - this is the combination the reported bug was about).
    SendOk 'create_material KitchenSinkMaterial materials\test.mat' | Out-Null
    SendOk 'create_multi_material KitchenSinkBlend materials\test.mat' | Out-Null
    SendOk 'add_layer KitchenSinkBlend TestRed' | Out-Null
    SendOk "set_layer KitchenSinkBlend 0 ""{'value':0.6}""" | Out-Null
    SendOk 'set_multi_material KitchenSinkMaterial KitchenSinkBlend' | Out-Null

    # An entity group.
    SendOk 'create_group KitchenSinkGroup' | Out-Null
    SendOk "set_group $kitchenInstance KitchenSinkGroup" | Out-Null

    # Editor-only view state (grid).
    SendOk 'set_grid_size 0.5 30 0.2' | Out-Null

    # The one save an actual user reaches for.
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-kitchen-sink'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $names = Get-EntityNames -Session $reloaded

        Assert-Contains -Collection $names -Value 'kitchen_sink_entity' -Message 'the created entity reloaded'
        Assert-Vector3Near -Expected @{ x = 1.0; y = 2.0; z = 3.0 } `
            -Actual (Get-Position -Session $reloaded -Entity 'kitchen_sink_entity') -Tolerance 0.001

        Assert-Contains -Collection $names -Value 'kitchen_sink_box' -Message 'the renamed/moved entity reloaded'
        Assert-Vector3Near -Expected @{ x = 9.0; y = 0.0; z = 0.0 } `
            -Actual (Get-Position -Session $reloaded -Entity 'kitchen_sink_box') -Tolerance 0.001

        Assert-Contains -Collection $names -Value $kitchenInstance -Message 'the placed instance reloaded'
        Assert-Vector3Near -Expected @{ x = -5.0; y = 0.0; z = -5.0 } `
            -Actual (Get-Position -Session $reloaded -Entity $kitchenInstance) -Tolerance 0.001

        $blocks = Get-TemplateInfo -Session $reloaded -Template 'kitchen_sink_template'
        Assert-Near -Expected 2.0 -Actual $blocks['Transform'].scale.x -Tolerance 0.001 `
            -Message 'the template edit reloaded'

        $matNames = @((Invoke-EditorCommand -Session $reloaded -Command 'materials')[0].Payload |
            ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $matNames -Value 'KitchenSinkMaterial' -Message 'the material reloaded'

        $multiNames = @((Invoke-EditorCommand -Session $reloaded -Command 'multi_materials')[0].Payload |
            ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $multiNames -Value 'KitchenSinkBlend' -Message 'the multi-material reloaded'
        $layer = (Invoke-EditorCommand -Session $reloaded -Command 'layer KitchenSinkBlend 0')[0].Payload[0] |
            ConvertFrom-Json
        Assert-Equal -Expected 'TestRed' -Actual $layer.material -Message 'its layer reloaded'
        Assert-Near -Expected 0.6 -Actual $layer.value -Tolerance 0.001 -Message 'and its edited value'

        # The attachment (MaterialData::multi_material_name) lives in the
        # material's own file, so confirm it on disk rather than through a
        # dedicated command - same check the multi-materials suite uses.
        $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
        $sink = $mat.materials | Where-Object { $_.name -eq 'KitchenSinkMaterial' }
        Assert-Equal -Expected 'KitchenSinkBlend' -Actual $sink.multi_material -Message 'the attachment reloaded'

        $groups = (Invoke-EditorCommand -Session $reloaded -Command 'list_groups')[0]
        Assert-Match -Pattern 'KitchenSinkGroup' -Actual ($groups.Payload -join "`n") -Message 'the group reloaded'

        $state = Get-State -Session $reloaded
        Assert-Near -Expected 0.5 -Actual $state.grid_size -Tolerance 0.001 -Message 'grid settings reloaded'
        Assert-Near -Expected 30.0 -Actual $state.grid_rotation_step_degrees -Tolerance 0.001
        Assert-Near -Expected 0.2 -Actual $state.grid_scale_step -Tolerance 0.001
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
