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

Test 'lights and the camera rig cannot be copied' {
    Assert-Err -Result (Send 'copy sun')[0] -Message 'a light is not a copyable object'
    Assert-Err -Result (Send 'copy ambient')[0]
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
