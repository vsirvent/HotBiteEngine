# fixture: empty
# description: Templates - authoring, component blocks, storage (.tpl vs inline), placement, saving.

function TemplateLine {
    param([string]$Name)
    $r = SendOk 'list_templates'
    return ($r[0].Payload | Where-Object { $_ -like "$Name *" -or $_ -eq $Name })
}

Test 'list_templates reports both storage forms' {
    $r = SendOk 'list_templates'
    Assert-Equal -Expected '3 templates' -Actual $r[0].Text
    Assert-Match -Pattern 'in=file' -Actual (TemplateLine -Name 'tf_marker') -Message 'referenced as a .tpl'
    Assert-Match -Pattern 'in=level' -Actual (TemplateLine -Name 'tf_box') -Message 'inline in the level'
}

Test 'create_template makes a bare template, like Add/Entity' {
    # Base+Transform only - the two the component registry itself marks
    # entity-mandatory. A template used to force Mesh/Material/Bounds on top of
    # these so it was immediately a placeable cube; now it starts exactly as
    # minimal as a bare scene entity and the user adds whatever it needs.
    SendOk 'create_template widget' | Out-Null
    Assert-Match -Pattern 'unsaved' -Actual (TemplateLine -Name 'widget')
    $blocks = Get-TemplateInfo -Session $Session -Template 'widget'
    foreach ($c in @('Base', 'Transform')) {
        Assert-True -Condition $blocks.ContainsKey($c) -Message "a new template declares $c"
    }
    foreach ($c in @('Mesh', 'Material', 'Bounds')) {
        Assert-False -Condition $blocks.ContainsKey($c) -Message "a new template no longer forces $c"
    }
    # Still placeable - an invisible marker with nothing to render, exactly as a
    # bare Add/Entity would be, since World::SpawnTemplateEntities no longer needs
    # Mesh/Bounds to spawn something.
    $placed = Get-PlacedInstance -Result (Send 'place widget')[0]
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value $placed.Name -Message 'after place'
    Assert-Near -Expected 0.0 -Actual $placed.X -Tolerance 0.0001 -Message 'origin placement is reproducible'
}

Test 'Mesh, Material and Bounds are ordinary addable/removable components on a template' {
    foreach ($c in @('Mesh', 'Material', 'Bounds')) {
        SendOk "template_add_component widget $c" | Out-Null
    }
    $blocks = Get-TemplateInfo -Session $Session -Template 'widget'
    foreach ($c in @('Mesh', 'Material', 'Bounds')) {
        Assert-True -Condition $blocks.ContainsKey($c) -Message "$c was added"
    }
    # Placeable as the built-in cube now, the same shape the old forced defaulting
    # produced - the difference is that this was asked for, not assumed.
    $placed = Get-PlacedInstance -Result (Send 'place widget')[0]
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value $placed.Name

    SendOk 'template_remove_component widget Bounds' | Out-Null
    Assert-False -Condition (Get-TemplateInfo -Session $Session -Template 'widget').ContainsKey('Bounds') `
        -Message 'Bounds can be removed again, like any other component'
    SendOk 'template_add_component widget Bounds' | Out-Null
}

Test 'create_template rejects a duplicate name' {
    Assert-Err -Result (Send 'create_template widget')[0]
}

Test 'template_set adds or updates a whole component block' {
    SendOk "template_set widget Transform ""{'scale':{'x':2.0,'y':2.0,'z':2.0}}""" | Out-Null
    $blocks = Get-TemplateInfo -Session $Session -Template 'widget'
    Assert-Near -Expected 2.0 -Actual $blocks['Transform'].scale.x -Tolerance 0.001
    # ...and the next instance is spawned with it.
    $placed = Get-PlacedInstance -Result (Send 'place widget')[0]
    $t = Get-Component -Session $Session -Entity $placed.Name -Component 'Transform'
    Assert-Vector3Near -Expected @{ x = 2.0; y = 2.0; z = 2.0 } -Actual $t.scale
}

Test 'template_set needs single-quoted JSON' {
    # The tokenizer strips double quotes, so a double-quoted object never arrives
    # intact - it must fail rather than half-apply.
    Assert-Err -Result (Send 'template_set widget Transform {"scale":{"x":3.0}}')[0]
    $blocks = Get-TemplateInfo -Session $Session -Template 'widget'
    Assert-Near -Expected 2.0 -Actual $blocks['Transform'].scale.x -Tolerance 0.001 -Message 'unchanged'
}

Test 'template_add_component and template_remove_component' {
    SendOk 'template_add_component widget Physics' | Out-Null
    Assert-True -Condition (Get-TemplateInfo -Session $Session -Template 'widget').ContainsKey('Physics')
    SendOk "template_set widget Physics ""{'type':'DYNAMIC','shape':'SPHERE'}""" | Out-Null

    # An instance placed now carries the block, which is the whole point of a
    # template: it is not just a mesh and a material.
    $placed = Get-PlacedInstance -Result (Send 'place widget')[0]
    $physics = Get-Component -Session $Session -Entity $placed.Name -Component 'Physics'
    Assert-Equal -Expected 'DYNAMIC' -Actual $physics.type
    Assert-Equal -Expected 'SPHERE' -Actual $physics.shape

    SendOk 'template_remove_component widget Physics' | Out-Null
    Assert-False -Condition (Get-TemplateInfo -Session $Session -Template 'widget').ContainsKey('Physics')
}

Test 'Base and Transform cannot be removed' {
    foreach ($c in @('Base', 'Transform')) {
        Assert-Err -Result (Send "template_remove_component widget $c")[0] -Message "$c is mandatory"
    }
}

Test 'template_material points the template at a material' {
    SendOk 'template_material widget TestRed' | Out-Null
    Assert-Equal -Expected 'TestRed' -Actual (Get-TemplateInfo -Session $Session -Template 'widget')['Material'].name
    $placed = Get-PlacedInstance -Result (Send 'place widget')[0]
    Assert-Equal -Expected 'TestRed' -Actual (Get-Component -Session $Session -Entity $placed.Name -Component 'Material').name
}

Test 'template_mesh and template_material name assets rather than resolve them' {
    # A template records the *name* of a mesh and a material; both are resolved at
    # spawn, and a level that has not loaded the asset yet is a normal state (the
    # Templates section of World::Load runs after materials and meshes for exactly
    # this reason). So an unknown name is accepted and shows as unresolved, while
    # an unknown *template* - which is a typo in the command, not authoring - is not.
    Assert-Ok -Result (Send 'template_material widget not_loaded_yet')[0]
    Assert-Equal -Expected 'not_loaded_yet' -Actual (Get-TemplateInfo -Session $Session -Template 'widget')['Material'].name
    SendOk 'template_material widget TestRed' | Out-Null
    Assert-Err -Result (Send 'template_material no_such_template TestRed')[0]
    Assert-Err -Result (Send 'template_mesh no_such_template cube')[0]
}

Test 'duplicate_template copies an authored template' {
    SendOk 'duplicate_template widget widget2' | Out-Null
    $a = Get-TemplateInfo -Session $Session -Template 'widget'
    $b = Get-TemplateInfo -Session $Session -Template 'widget2'
    Assert-Near -Expected $a['Transform'].scale.x -Actual $b['Transform'].scale.x -Tolerance 0.0001
    Assert-Equal -Expected $a['Material'].name -Actual $b['Material'].name
    # Editing the copy leaves the original alone - it is a copy, not a reference.
    SendOk "template_set widget2 Transform ""{'scale':{'x':9.0,'y':9.0,'z':9.0}}""" | Out-Null
    Assert-Near -Expected 2.0 -Actual (Get-TemplateInfo -Session $Session -Template 'widget')['Transform'].scale.x -Tolerance 0.001
}

Test 'template_from_entity captures a scene entity' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_scale 1.5 1.5 1.5' | Out-Null
    SendOk 'template_from_entity box_a from_box' | Out-Null
    $blocks = Get-TemplateInfo -Session $Session -Template 'from_box'
    Assert-True -Condition $blocks.ContainsKey('Mesh') -Message 'the entity mesh came across'
    Assert-Near -Expected 1.5 -Actual $blocks['Transform'].scale.x -Tolerance 0.001
}

Test 'template_from_entity carries only what the source entity actually has' {
    # A mesh-less entity (Add/Entity's bare Base+Transform) used to be refused
    # outright - "has no Mesh, so it cannot become a template" - purely because a
    # mesh-less template used to be unspawnable. Now it is accepted, and
    # SerializeEntityAsTemplate copies only what is actually there: no Mesh, no
    # Material, no Bounds sneaked in on top of the source entity's real component
    # set.
    SendOk 'menu "Add/Entity"' | Out-Null
    SendOk 'template_from_entity Entity from_marker' | Out-Null
    $blocks = Get-TemplateInfo -Session $Session -Template 'from_marker'
    foreach ($c in @('Base', 'Transform')) {
        Assert-True -Condition $blocks.ContainsKey($c) -Message "from_marker carries $c"
    }
    foreach ($c in @('Mesh', 'Material', 'Bounds')) {
        Assert-False -Condition $blocks.ContainsKey($c) -Message "from_marker does not gain $c"
    }
    Assert-Ok -Result (Send 'place from_marker')[0] -Message 'a mesh-less template is still placeable'
}

Test 'template_storage moves a template between .tpl and inline' {
    $tpl = Join-Path $Assets 'Templates\widget.tpl'
    SendOk 'template_storage widget file' | Out-Null
    Assert-Match -Pattern 'in=file' -Actual (TemplateLine -Name 'widget')
    SendOk 'save_templates' | Out-Null
    Assert-FileExists -Path $tpl -Message 'the .tpl'

    SendOk 'template_storage widget level' | Out-Null
    Assert-Match -Pattern 'in=level' -Actual (TemplateLine -Name 'widget')
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $inline = $level.world.templates | Where-Object { $_.name -eq 'widget' }
    Assert-True -Condition ($null -ne $inline) -Message 'the level carries the definition'
    Assert-True -Condition ($null -ne $inline.components) -Message 'inline form has components'
}

Test 'template_storage rejects an unknown mode' {
    Assert-Err -Result (Send 'template_storage widget elsewhere')[0] -Pattern 'usage:'
}

Test 'File/Save Level flushes templates first' {
    # The level names the .tpl files, so a reference to one that was never written
    # would produce a level that cannot reload.
    SendOk 'create_template flushed' | Out-Null
    SendOk 'template_storage flushed file' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Templates\flushed.tpl') -Message 'the .tpl the level now references'
    Assert-Equal -Expected 0 -Actual (Get-State -Session $Session).unsaved_templates
}

Test 'import_template copies a .tpl into the project' {
    $source = Join-Path $ShotDir 'imported.tpl'
    $definition = @{
        name = 'imported'
        components = @{ Transform = @{ scale = @{ x = 4.0; y = 4.0; z = 4.0 } } }
    } | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($source, $definition)

    SendOk "import_template ""$source""" | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Templates\imported.tpl') -Message 'copied into <assets>/Templates'
    Assert-Near -Expected 4.0 -Actual (Get-TemplateInfo -Session $Session -Template 'imported')['Transform'].scale.x -Tolerance 0.001
    Assert-Ok -Result (Send 'place imported')[0] -Message 'an imported template is placeable'
}

Test 'select_template selects, and an unknown one is an error' {
    SendOk 'select_template widget' | Out-Null
    Assert-Match -Pattern '\[selected\]' -Actual (TemplateLine -Name 'widget')
    Assert-Err -Result (Send 'select_template nope')[0] -Pattern 'unknown template'
}

Test 'remove_template unregisters it' {
    SendOk 'remove_template widget2' | Out-Null
    $names = @((SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'widget2' -Message 'template list'
    Assert-Err -Result (Send 'place widget2')[0] -Message 'and it is no longer placeable'
    SendOk 'undo' | Out-Null
    $names = @((SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'widget2' -Message 'removal is undoable until templates are saved'
}

Test 'place rejects an unknown template and an unknown placement mode' {
    Assert-Err -Result (Send 'place no_such_template')[0]
    Assert-Err -Result (Send 'place widget sideways')[0] -Pattern 'unknown placement'
}

Test 'place view drops the object on what the view is looking at' {
    SendOk 'camera_pos 0 25 0', 'camera_target 0 0 0' | Out-Null
    $r = Send 'place widget view'
    Assert-Ok -Result $r[0]
    $placed = Get-PlacedInstance -Result $r[0]
    # Nothing is under that ray in this fixture, so it falls back to 15 units
    # down it rather than failing.
    Assert-True -Condition ($placed.Y -lt 25.0) -Message 'placed along the view ray, not at the camera'
}
