# fixture: empty
# description: copy / cut / paste / delete, and the parking that keeps a cut undoable.

Test 'copy then paste creates a second object' {
    $before = Get-EntityNames -Session $Session
    SendOk 'copy box_a' | Out-Null
    SendOk 'paste' | Out-Null
    $after = Get-EntityNames -Session $Session
    $new = @($after | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'exactly one entity appeared'
    Assert-Match -Pattern '^box_a' -Actual $new[0] -Message 'the copy is named after its source'
    Assert-Match -Pattern '_copy' -Actual $new[0]
}

Test 'a pasted instance is a real instance of the same template' {
    $before = Get-EntityNames -Session $Session
    SendOk 'copy box_b', 'paste' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count
    # It draws the same mesh and answers to the same commands as its source.
    $sourceMesh = Get-Component -Session $Session -Entity 'box_b' -Component 'Mesh'
    $copyMesh = Get-Component -Session $Session -Entity $new[0] -Component 'Mesh'
    Assert-Equal -Expected $sourceMesh.name -Actual $copyMesh.name
    Assert-Ok -Result (Send "select $($new[0])")[0]
}

Test 'paste with an empty clipboard is an error' {
    # Nothing has been copied in this session before the tests above, so this runs
    # after them and instead checks the error path on a cleared selection.
    Assert-Err -Result (Send 'copy nope')[0] -Pattern 'entity not found'
}

Test 'cut hides the entity but keeps it pasteable' {
    SendOk 'cut box_c' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value 'box_c' `
        -Message 'a cut entity is filtered out of list_entities'
    $before = Get-EntityNames -Session $Session
    SendOk 'paste' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'the cut source can still be pasted'
}

Test 'cut is undoable' {
    SendOk 'cut box_b' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value 'box_b' -Message 'after cut'
    SendOk 'undo' | Out-Null
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value 'box_b' -Message 'after undo'
}

Test 'delete removes the selection as one step and undo brings it back' {
    SendOk 'select box_a box_b' | Out-Null
    SendOk 'delete' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-NotContains -Collection $names -Value 'box_a' -Message 'after delete'
    Assert-NotContains -Collection $names -Value 'box_b' -Message 'after delete'

    SendOk 'undo' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-Contains -Collection $names -Value 'box_a' -Message 'one undo restored both'
    Assert-Contains -Collection $names -Value 'box_b' -Message 'one undo restored both'
}

Test 'delete with nothing selected is an error' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'delete')[0]
}

Test 'a light can be copied and pasted as an independent entity' {
    # World::CloneEntity round-trips every component the source has through its own
    # ToJson/FromJson (see the comment on CloneEntity), which is what lets a
    # PointLight/DirectionalLight - components that own a live GPU shadow-map
    # resource - be cloned safely: the clone allocates its own resource instead of
    # aliasing the source's.
    $before = Get-EntityNames -Session $Session
    SendOk 'copy sun', 'paste' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'exactly one entity appeared'
    Assert-Match -Pattern '^sun_copy' -Actual $new[0]
    $source = Get-Component -Session $Session -Entity 'sun' -Component 'DirectionalLight'
    $copy = Get-Component -Session $Session -Entity $new[0] -Component 'DirectionalLight'
    Assert-Near -Expected $source.intensity -Actual $copy.intensity -Tolerance 0.001 `
        -Message 'the clone carries the same light data'
}

Test 'an ambient light can be copied and pasted too' {
    $before = Get-EntityNames -Session $Session
    SendOk 'copy ambient', 'paste' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count
    Assert-Match -Pattern '^ambient_copy' -Actual $new[0]
}

Test 'a point light added from the preset can be deleted outright' {
    SendOk 'menu "Add/Point Light"' | Out-Null
    $names = Get-EntityNames -Session $Session
    $lightName = @($names | Where-Object { $_ -match '^PointLight' })[0]
    Assert-True -Condition ($null -ne $lightName) -Message 'Add/Point Light created an entity'
    SendOk "select $lightName", 'delete' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value $lightName
}

Test 'the sky is a singleton and still cannot be copied' {
    # RenderSystem asserts exactly one Sky is ever registered, so this is the one
    # entity CloneEntity/CanCopySelected still refuse - everything else in the
    # previous two tests is deliberately new behaviour.
    SendOk 'menu "Add/Sky"' | Out-Null
    $names = Get-EntityNames -Session $Session
    $skyName = @($names | Where-Object { $_ -match '^Sky' })[0]
    Assert-True -Condition ($null -ne $skyName) -Message 'Add/Sky created an entity'
    Assert-Err -Result (Send "copy $skyName")[0] -Pattern 'singleton'
    SendOk "select $skyName", 'delete' | Out-Null
}

Test 'the Edit menu drives the same clipboard' {
    $before = Get-EntityNames -Session $Session
    SendOk 'select box_a' | Out-Null
    SendOk 'menu "Edit/Copy"', 'menu "Edit/Paste"' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'menu Copy/Paste is the same code path'
}

Test 'a cut entity stays out of a saved level' {
    SendOk 'cut box_a' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $names = @($level.world.instances | ForEach-Object { $_.name })
    Assert-NotContains -Collection $names -Value 'box_a' -Message 'saved instances'
}
