# fixture: empty
# description: Add/Entity - an entity built from nothing: what it carries, how it is undone, deleted, and what the level records.

Test 'Add/Entity is in the menu registry' {
    $r = SendOk 'menus'
    $paths = @($r[0].Payload | ForEach-Object { ($_ -replace ' \(disabled\)$', '') })
    Assert-Contains -Collection $paths -Value 'Add/Entity' -Message 'menu registry'
}

Test 'creating one gives an entity carrying only the mandatory components' {
    $before = Get-EntityNames -Session $Session
    $r = SendOk 'menu "Add/Entity"'
    Assert-Match -Pattern 'Created empty entity: Entity$' -Actual $r[0].Text
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'exactly one entity appeared'
    Assert-Equal -Expected 'Entity' -Actual $new[0]

    # Base and Transform, the two the registry marks Mandatory, and nothing else:
    # no mesh, no material, no bounds, no body.
    $components = (SendOk 'components Entity')[0].Payload
    Assert-Equal -Expected 2 -Actual $components.Count -Message 'only the required components'
    Assert-Equal -Expected 'Base' -Actual $components[0]
    Assert-Equal -Expected 'Transform' -Actual $components[1]
}

Test 'the new entity is selected, so the Components panel is already on it' {
    SendOk 'deselect' | Out-Null
    SendOk 'menu "Add/Entity"' | Out-Null
    $state = Get-State -Session $Session
    Assert-Equal -Expected 1 -Actual $state.selected_count
    Assert-Equal -Expected 'Entity_1' -Actual $state.selected_entity_name -Message 'named apart from the first'
}

Test 'creating one is undoable, and redo brings it back by name' {
    SendOk 'menu "Add/Entity"' | Out-Null
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value 'Entity_2' -Message 'after create'

    $r = SendOk 'undo'
    Assert-Match -Pattern 'create Entity_2' -Actual $r[0].Text
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value 'Entity_2' -Message 'after undo'

    SendOk 'redo' | Out-Null
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value 'Entity_2' -Message 'after redo'
}

Test 'an empty entity takes a transform edit like any other' {
    SendOk 'select Entity', 'set_position 4 1 2' | Out-Null
    Assert-Vector3Near -Expected @{ x = 4.0; y = 1.0; z = 2.0 } `
        -Actual (Get-Position -Session $Session -Entity 'Entity')
}

Test 'components can be added to it, and that is what makes it visible' {
    # The intended workflow: an entity from nothing, then the components that say
    # what it is. Mesh and Material come with stand-in assets (a unit cube and
    # plain white), so the entity draws as soon as it has Mesh, Material, Bounds.
    SendOk 'add_component Entity Mesh', 'add_component Entity Bounds', 'add_component Entity Material' | Out-Null
    $components = (SendOk 'components Entity')[0].Payload
    foreach ($c in @('Base', 'Transform', 'Mesh', 'Material', 'Bounds')) {
        Assert-Contains -Collection $components -Value $c -Message 'components of a built-up entity'
    }
    Assert-Equal -Expected '__default_mesh' `
        -Actual (Get-Component -Session $Session -Entity 'Entity' -Component 'Mesh').name
}

Test 'delete removes one whatever it carries, and undo brings it back' {
    # It has no mesh, so the rule that makes a scene entity deletable does not
    # apply to it - refusing would make Add/Entity a one-way door.
    SendOk 'select Entity_2', 'delete' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value 'Entity_2' -Message 'after delete'
    SendOk 'undo' | Out-Null
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value 'Entity_2' -Message 'after undo'
}

Test 'a save records it under created_entities, with its pose and its components' {
    SendOk 'rename Entity_1 marker_point' | Out-Null
    SendOk 'select Entity_2', 'delete' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $records = @($level.world.created_entities)
    $names = @($records | ForEach-Object { $_.name })
    Assert-Contains -Collection $names -Value 'Entity' -Message 'saved created entities'
    Assert-Contains -Collection $names -Value 'marker_point' -Message 'a rename follows the record'
    Assert-NotContains -Collection $names -Value 'Entity_1' -Message 'the old name is gone'
    Assert-NotContains -Collection $names -Value 'Entity_2' -Message 'the deleted one stops being listed'

    $record = $records | Where-Object { $_.name -eq 'Entity' }
    Assert-Near -Expected 4.0 -Actual $record.position.x -Tolerance 0.001
    Assert-Near -Expected 1.0 -Actual $record.position.y -Tolerance 0.001
    Assert-Equal -Expected '__default_mesh' -Actual $record.components.Mesh.name -Message 'its components ride along'

    # The record carries everything, so there is no second, partial copy of it in
    # the "entities" override array.
    $overrides = @($level.world.entities | ForEach-Object { $_.name })
    Assert-NotContains -Collection $overrides -Value 'Entity' -Message 'entities overrides'
}

Test 'a reloaded level rebuilds them from that record alone' {
    # Nothing else in the file implies these entities exist: no model, no template,
    # no source entity. A second process on the file the first one just wrote is
    # the only check that "created_entities" is really enough.
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-created'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $names = Get-EntityNames -Session $reloaded
        Assert-Contains -Collection $names -Value 'Entity' -Message 'reloaded scene'
        Assert-Contains -Collection $names -Value 'marker_point' -Message 'reloaded scene'
        Assert-NotContains -Collection $names -Value 'Entity_2' -Message 'the deleted one stayed deleted'

        Assert-Vector3Near -Expected @{ x = 4.0; y = 1.0; z = 2.0 } `
            -Actual (Get-Position -Session $reloaded -Entity 'Entity') -Tolerance 0.001 `
            -Message 'and at the same place'
        Assert-Equal -Expected '__default_mesh' `
            -Actual (Get-Component -Session $reloaded -Entity 'Entity' -Component 'Mesh').name `
            -Message 'with the components it was given'

        # The one with nothing on it is still an entity, and still empty.
        $bare = (Invoke-EditorCommand -Session $reloaded -Command 'components marker_point')[0]
        Assert-Equal -Expected 2 -Actual $bare.Payload.Count -Message 'Base and Transform'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'one given a Mesh but no Bounds still reloads' {
    # World::Init sizes a collider from Bounds::local_box and would otherwise throw
    # reading one that is not there - out of an Init() nothing catches, so the level
    # that saved would not reopen. Only an entity assembled by hand can be in this
    # state; everything an FBX or a template produces carries both.
    SendOk 'menu "Add/Entity"' | Out-Null
    $bare = (Get-State -Session $Session).selected_entity_name
    SendOk "add_component $bare Mesh" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-nobounds'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        Assert-Contains -Collection (Get-EntityNames -Session $reloaded) -Value $bare -Message 'reloaded scene'
        $components = (Invoke-EditorCommand -Session $reloaded -Command "components $bare")[0]
        Assert-Contains -Collection $components.Payload -Value 'Mesh' -Message 'with its mesh'
        Assert-NotContains -Collection $components.Payload -Value 'Bounds' -Message 'and still no bounds'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
