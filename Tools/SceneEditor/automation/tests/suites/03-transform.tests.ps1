# fixture: empty
# description: Transform edits through the Inspector code path, multi-entity edits, spawn-space instance records.

Test 'set_position edits the selected entity' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 1 2 3' | Out-Null
    Assert-Vector3Near -Expected @{ x = 1.0; y = 2.0; z = 3.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
}

Test 'set_scale edits the selected entity' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_scale 2 0.5 3' | Out-Null
    $t = Get-Component -Session $Session -Entity 'box_a' -Component 'Transform'
    Assert-Vector3Near -Expected @{ x = 2.0; y = 0.5; z = 3.0 } -Actual $t.scale
    SendOk 'set_scale 1 1 1' | Out-Null
}

Test 'set_rotation takes pitch/yaw/roll in degrees' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_rotation 0 90 0' | Out-Null
    $t = Get-Component -Session $Session -Entity 'box_a' -Component 'Transform'
    # 90 degrees about Y is the quaternion (0, sin45, 0, cos45).
    Assert-Near -Expected 0.70710678 -Actual $t.rotation.y -Tolerance 0.001 -Message 'quaternion y'
    Assert-Near -Expected 0.70710678 -Actual $t.rotation.w -Tolerance 0.001 -Message 'quaternion w'
    Assert-Near -Expected 0.0 -Actual $t.rotation.x -Tolerance 0.001
    Assert-Near -Expected 0.0 -Actual $t.rotation.z -Tolerance 0.001
    SendOk 'set_rotation 0 0 0' | Out-Null
}

Test 'a transform command with a bad argument count is rejected' {
    SendOk 'select box_a' | Out-Null
    Assert-Err -Result (Send 'set_position 1 2')[0] -Pattern 'usage:'
    Assert-Err -Result (Send 'set_scale x y z')[0] -Pattern 'usage:'
}

Test 'a transform edit with nothing selected is rejected' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'set_position 0 0 0')[0]
}

Test 'a typed transform edits the primary, not the whole selection' {
    # Inspector::ApplyTransform works on state.selected_entity - the primary, the
    # entity whose numbers the Inspector fields are showing. Only an interactive
    # gizmo drag moves a whole selection; typing a coordinate for several objects
    # at once would put them all in the same place.
    SendOk 'select box_a box_b' | Out-Null
    SendOk 'set_position 5 5 5' | Out-Null
    Assert-Vector3Near -Expected @{ x = 5.0; y = 5.0; z = 5.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_b') -Message 'the primary moved'
    Assert-Vector3Near -Expected @{ x = 1.0; y = 2.0; z = 3.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a') -Message 'the other pick did not'
    SendOk 'undo' | Out-Null
    Assert-Vector3Near -Expected @{ x = 3.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_b') -Message 'and it undoes'
}

Test 'set_component Transform is the same edit as set_position' {
    SendOk 'select box_b' | Out-Null
    SendOk "set_component box_b Transform ""{'position':{'x':-7.0,'y':1.5,'z':2.0}}""" | Out-Null
    Assert-Vector3Near -Expected @{ x = -7.0; y = 1.5; z = 2.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_b')
    # Keys left out keep their values.
    $t = Get-Component -Session $Session -Entity 'box_b' -Component 'Transform'
    Assert-Vector3Near -Expected @{ x = 1.0; y = 1.0; z = 1.0 } -Actual $t.scale -Message 'scale untouched'
}

Test 'a moved instance is stored in spawn space, not world space' {
    # World::SpawnInstance composes the record with the template's base transform,
    # so a template scaled 0.5 and an instance record of 4 make an object of 2.
    # The scaled template here is the file-backed tf_marker (scale 0.5).
    $placed = Get-PlacedInstance -Result (Send 'place tf_marker')[0]
    SendOk "select $($placed.Name)" | Out-Null
    $t = Get-Component -Session $Session -Entity $placed.Name -Component 'Transform'
    Assert-Vector3Near -Expected @{ x = 0.5; y = 0.5; z = 0.5 } -Actual $t.scale `
        -Message 'the spawned object carries the template base scale'

    SendOk 'set_scale 2 2 2' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $record = $level.world.instances | Where-Object { $_.name -eq $placed.Name }
    Assert-True -Condition ($null -ne $record) -Message 'the instance is in the level file'
    Assert-Near -Expected 4.0 -Actual $record.scale.x -Tolerance 0.001 `
        -Message 'record is world scale 2 divided by the template base 0.5'
}

Test 'the gizmo mode is a mode, not an edit' {
    SendOk 'select box_a' | Out-Null
    $before = Get-Position -Session $Session -Entity 'box_a'
    SendOk 'menu "Edit/Gizmo: Scale"', 'menu "Edit/Gizmo: Rotate"', 'menu "Edit/Gizmo: Translate"' | Out-Null
    Assert-Vector3Near -Expected $before -Actual (Get-Position -Session $Session -Entity 'box_a')
}
