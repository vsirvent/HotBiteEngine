# fixture: empty
# description: Grid snapping (GridSnap.h) - the gizmo drag path (SelectionGizmo::SimulateDrag), template placement, and persistence.

Test 'grid snap is off by default and the View menu toggles it' {
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_snap_enabled
    SendOk 'menu "View/Grid Snap"' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-State -Session $Session).grid_snap_enabled
    SendOk 'menu "View/Grid Snap"' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_snap_enabled
}

Test 'grid lines are hidden by default and independent of grid snap' {
    # GridOverlay::Draw is gated on grid_lines_visible, not grid_snap_enabled -
    # showing the reference lines and snapping to them are separate switches.
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_lines_visible
    SendOk 'menu "View/Grid Snap"' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_lines_visible `
        -Message 'enabling snap must not also show the lines'
    SendOk 'menu "View/Grid Snap"' | Out-Null

    SendOk 'menu "View/Grid Lines"' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-State -Session $Session).grid_lines_visible
    SendOk 'menu "View/Grid Lines"' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_lines_visible

    SendOk 'set_grid_visible 1' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-State -Session $Session).grid_lines_visible
    SendOk 'set_grid_visible 0' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_lines_visible
}

Test 'set_grid_visible validates its arguments' {
    Assert-Err -Result (Send 'set_grid_visible')[0] -Pattern 'usage:'
}

Test 'set_grid_size sets the three step values' {
    SendOk 'set_grid_size 2 30 0.25' | Out-Null
    $state = Get-State -Session $Session
    Assert-Near -Expected 2.0 -Actual $state.grid_size
    Assert-Near -Expected 30.0 -Actual $state.grid_rotation_step_degrees
    Assert-Near -Expected 0.25 -Actual $state.grid_scale_step
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
}

Test 'set_grid_size rejects a non-numeric size' {
    Assert-Err -Result (Send 'set_grid_size not-a-number')[0] -Pattern 'usage:'
}

Test 'a translate drag snaps to the position grid' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 0 0 0' | Out-Null
    SendOk 'set_rotation 0 0 0' | Out-Null
    SendOk 'set_grid_size 2' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'simulate_gizmo_drag translate x 3.2' | Out-Null
    Assert-Vector3Near -Expected @{ x = 4.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a') `
        -Message 'nearest multiple of the 2-unit grid'
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
    SendOk 'set_position 0 0 0' | Out-Null
}

Test 'the same drag is unsnapped while grid snap is off' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 0 0 0' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).grid_snap_enabled
    SendOk 'set_grid_size 2' | Out-Null
    SendOk 'simulate_gizmo_drag translate x 3.2' | Out-Null
    Assert-Vector3Near -Expected @{ x = 3.2; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a') -Tolerance 0.01
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
    SendOk 'set_position 0 0 0' | Out-Null
}

Test 'a rotate drag snaps to the rotation grid' {
    # ApplyRotate snaps the total angle since mouse-down before turning it into a
    # quaternion, so the expected quaternion is a pure rotation about Y by the
    # nearest multiple of the step (100 degrees, 15-degree step -> 105 degrees).
    SendOk 'select box_a' | Out-Null
    SendOk 'set_rotation 0 0 0' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'simulate_gizmo_drag rotate y 100' | Out-Null
    $t = Get-Component -Session $Session -Entity 'box_a' -Component 'Transform'
    $halfRad = [Math]::PI * 105.0 / 180.0 / 2.0
    Assert-Near -Expected ([Math]::Sin($halfRad)) -Actual $t.rotation.y -Tolerance 0.001 -Message 'quaternion y'
    Assert-Near -Expected ([Math]::Cos($halfRad)) -Actual $t.rotation.w -Tolerance 0.001 -Message 'quaternion w'
    Assert-Near -Expected 0.0 -Actual $t.rotation.x -Tolerance 0.001
    Assert-Near -Expected 0.0 -Actual $t.rotation.z -Tolerance 0.001
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_rotation 0 0 0' | Out-Null
}

Test 'a scale drag snaps to the scale grid, one axis at a time' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_scale 1 1 1' | Out-Null
    SendOk 'set_grid_size 1 15 0.2' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'simulate_gizmo_drag scale x 1.85' | Out-Null
    $t = Get-Component -Session $Session -Entity 'box_a' -Component 'Transform'
    Assert-Near -Expected 1.8 -Actual $t.scale.x -Tolerance 0.001 -Message 'nearest multiple of the 0.2 step'
    Assert-Near -Expected 1.0 -Actual $t.scale.y -Tolerance 0.001 -Message 'the other axes are untouched'
    Assert-Near -Expected 1.0 -Actual $t.scale.z -Tolerance 0.001
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
    SendOk 'set_scale 1 1 1' | Out-Null
}

Test 'simulate_gizmo_drag validates its arguments' {
    Assert-Err -Result (Send 'simulate_gizmo_drag')[0] -Pattern 'usage:'
    Assert-Err -Result (Send 'simulate_gizmo_drag sideways x 1')[0] -Pattern 'unknown mode'
    Assert-Err -Result (Send 'simulate_gizmo_drag translate diagonal 1')[0] -Pattern 'unknown axis'
    Assert-Err -Result (Send 'simulate_gizmo_drag translate x not-a-number')[0]
}

Test 'a typed set_position bypasses grid snap entirely' {
    # Only the gizmo drag path snaps (SelectionGizmo.cpp) - Inspector::ApplyTransform,
    # which set_position/set_scale/set_rotation call, is untouched by this feature.
    SendOk 'select box_a' | Out-Null
    SendOk 'set_grid_size 5' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_position 1.3 0 0' | Out-Null
    Assert-Vector3Near -Expected @{ x = 1.3; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
    SendOk 'set_position 0 0 0' | Out-Null
}

Test 'place view snaps the drop position when grid snap is on' {
    SendOk 'camera_pos 0 25 0', 'camera_target 0 0 0' | Out-Null
    SendOk 'set_grid_size 2' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    $placed = Get-PlacedInstance -Result (Send 'place tf_marker view')[0]
    $rem = [Math]::Abs($placed.X / 2.0 - [Math]::Round($placed.X / 2.0))
    Assert-True -Condition ($rem -lt 0.001) -Message "placed x=$($placed.X) is not on the 2-unit grid"
    $rem = [Math]::Abs($placed.Z / 2.0 - [Math]::Round($placed.Z / 2.0))
    Assert-True -Condition ($rem -lt 0.001) -Message "placed z=$($placed.Z) is not on the 2-unit grid"
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
}

Test 'grid settings persist in the level file' {
    SendOk 'set_grid_size 3 20 0.4' | Out-Null
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'menu "View/Grid Lines"' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    Assert-True -Condition ($null -ne $level.editor.grid) -Message 'editor.grid is written'
    Assert-Equal -Expected 'True' -Actual $level.editor.grid.enabled
    Assert-Near -Expected 3.0 -Actual $level.editor.grid.size
    Assert-Near -Expected 20.0 -Actual $level.editor.grid.rotation_step_degrees
    Assert-Near -Expected 0.4 -Actual $level.editor.grid.scale_step
    Assert-Equal -Expected 'True' -Actual $level.editor.grid.lines_visible
    SendOk 'menu "View/Grid Snap"' | Out-Null
    SendOk 'menu "View/Grid Lines"' | Out-Null
    SendOk 'set_grid_size 1 15 0.1' | Out-Null
}
