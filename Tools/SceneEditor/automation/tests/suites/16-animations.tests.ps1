# fixture: models
# description: A template's animation library - logical names over imported clips, defaults, and per-instance overrides.

Test 'the fixture template carries a library, not raw clip names' {
    $r = SendOk 'template_animations tf_troll'
    Assert-Equal -Expected '2 animations on template tf_troll' -Actual $r[0].Text
    $idle = $r[0].Payload | Where-Object { $_ -like 'idle *' }
    Assert-Match -Pattern 'clip=troll_idle' -Actual $idle
    Assert-Match -Pattern 'model=troll_idle' -Actual $idle -Message 'the model each clip came from'
    Assert-Match -Pattern '\[default\]' -Actual $idle -Message 'what an instance starts in'
}

Test 'template_add_animation adds a library entry and attaches the set' {
    SendOk 'create_template anim_rig' | Out-Null
    SendOk 'template_mesh anim_rig troll' | Out-Null
    $r = SendOk 'template_add_animation anim_rig run troll_walk'
    Assert-Equal -Expected 'anim_rig run -> troll_walk' -Actual $r[0].Text
    $line = (SendOk 'template_animations anim_rig')[0].Payload | Where-Object { $_ -like 'run *' }
    Assert-Match -Pattern 'clip=troll_walk' -Actual $line
    Assert-Match -Pattern '\[default\]' -Actual $line -Message 'the first one added becomes the default'
}

Test 'template_add_animation rejects a clip no model offers' {
    Assert-Err -Result (Send 'template_add_animation anim_rig fly no_such_clip')[0]
    Assert-Err -Result (Send 'template_add_animation nope walk troll_walk')[0]
}

Test 'template_rename_animation carries the default over' {
    SendOk 'template_rename_animation anim_rig run sprint' | Out-Null
    $r = SendOk 'template_animations anim_rig'
    $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'sprint' -Message 'library'
    Assert-NotContains -Collection $names -Value 'run' -Message 'library'
    Assert-Match -Pattern '\[default\]' -Actual ($r[0].Payload | Where-Object { $_ -like 'sprint *' })
}

Test 'template_default_animation picks what an instance starts in' {
    SendOk 'template_add_animation anim_rig stand troll_idle' | Out-Null
    SendOk 'template_default_animation anim_rig stand' | Out-Null
    $r = SendOk 'template_animations anim_rig'
    Assert-Match -Pattern '\[default\]' -Actual ($r[0].Payload | Where-Object { $_ -like 'stand *' })

    $placed = Get-PlacedInstance -Result (Send 'place anim_rig')[0]
    Assert-Equal -Expected 'stand' -Actual (Get-Component -Session $Session -Entity $placed.Name -Component 'Mesh').animation `
        -Message 'the instance reports the name it was asked for, not the clip file'
}

Test 'template_default_animation with no name means stand still' {
    SendOk 'template_default_animation anim_rig' | Out-Null
    $placed = Get-PlacedInstance -Result (Send 'place anim_rig')[0]
    Assert-Equal -Expected '' -Actual (Get-Component -Session $Session -Entity $placed.Name -Component 'Mesh').animation `
        -Message 'an explicit "play nothing" is distinct from never having chosen'
    SendOk 'template_default_animation anim_rig stand' | Out-Null
}

Test 'template_remove_animation drops the entry and the default with it' {
    SendOk 'template_remove_animation anim_rig stand' | Out-Null
    $r = SendOk 'template_animations anim_rig'
    $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'stand' -Message 'library'
    Assert-Err -Result (Send 'template_remove_animation anim_rig stand')[0] -Message 'and it is gone'
}

Test 'animations lists what an instance mesh can play and what it is playing' {
    $placed = Get-PlacedInstance -Result (Send 'place tf_troll')[0]
    $r = SendOk "animations $($placed.Name)"
    Assert-Match -Pattern 'animations on mesh troll' -Actual $r[0].Text
    Assert-Match -Pattern 'current: idle' -Actual $r[0].Text
    # The clips themselves are attached to the shared mesh asset, so they are
    # listed by their imported names here rather than by library name.
    Assert-Contains -Collection $r[0].Payload -Value 'troll_walk' -Message 'clips on the mesh'
    Assert-Err -Result (Send 'animations box_that_does_not_exist')[0]
    $script:trollInstance = $placed.Name
}

Test 'an instance picks its own animation out of the template library' {
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':'walk'}""" | Out-Null
    Assert-Equal -Expected 'walk' `
        -Actual (Get-Component -Session $Session -Entity $script:trollInstance -Component 'Mesh').animation

    # ...and its siblings keep the template default: the edit is per entity.
    $sibling = Get-PlacedInstance -Result (Send 'place tf_troll')[0]
    Assert-Equal -Expected 'idle' `
        -Actual (Get-Component -Session $Session -Entity $sibling.Name -Component 'Mesh').animation
}

Test 'an instance can be told to stand still' {
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':''}""" | Out-Null
    Assert-Equal -Expected '' `
        -Actual (Get-Component -Session $Session -Entity $script:trollInstance -Component 'Mesh').animation
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':'idle'}""" | Out-Null
}

Test 'loop and speed are arguments of the animation choice, not standalone fields' {
    # Mesh::FromJson reads animation_loop/animation_speed as parameters of
    # SetAnimation, so they only take effect alongside an "animation" key. On their
    # own they are silently ignored - which is what the panel avoids by always
    # sending the whole block.
    $before = Get-Component -Session $Session -Entity $script:trollInstance -Component 'Mesh'
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation_loop':false,'animation_speed':2.5}""" | Out-Null
    $alone = Get-Component -Session $Session -Entity $script:trollInstance -Component 'Mesh'
    Assert-Equal -Expected $before.animation_loop -Actual $alone.animation_loop -Message 'ignored on their own'

    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':'walk','animation_loop':false,'animation_speed':2.5}""" | Out-Null
    $together = Get-Component -Session $Session -Entity $script:trollInstance -Component 'Mesh'
    Assert-Equal -Expected 'False' -Actual $together.animation_loop
    Assert-Near -Expected 2.5 -Actual $together.animation_speed -Tolerance 0.001
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':'idle','animation_loop':true,'animation_speed':1.0}""" | Out-Null
}

Test 'the library survives a mesh swap' {
    # A library entry is a role this object plays; re-exporting the rig should
    # change one entry, not every caller.
    SendOk 'template_mesh anim_rig troll' | Out-Null
    $before = (SendOk 'template_animations anim_rig')[0].Text
    SendOk 'template_mesh anim_rig troll' | Out-Null
    Assert-Equal -Expected $before -Actual (SendOk 'template_animations anim_rig')[0].Text
}

Test 'the library round-trips through the level file' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $template = $level.world.templates | Where-Object { $_.name -eq 'tf_troll' }
    Assert-True -Condition ($null -ne $template) -Message 'the inline template is written'
    Assert-Equal -Expected 'troll_walk' -Actual $template.components.Mesh.clips.walk `
        -Message 'clips are stored as name -> imported clip'
}

Test 'a per-instance animation override reloads with the level' {
    SendOk "set_component $($script:trollInstance) Mesh ""{'animation':'walk'}""" | Out-Null
    SendOk "rename $($script:trollInstance) walking_troll" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-animations'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        Assert-Equal -Expected 'walk' `
            -Actual (Get-Component -Session $reloaded -Entity 'walking_troll' -Component 'Mesh').animation
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
