# fixture: empty
# description: The undo history contract - LIFO, what records and what deliberately does not, redo invalidation.

Test 'undo on an empty history is an error' {
    Assert-Err -Result (Send 'undo')[0]
    Assert-Err -Result (Send 'redo')[0]
}

Test 'undo names the step it applied' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 9 9 9' | Out-Null
    $r = SendOk 'undo'
    Assert-Match -Pattern 'transform' -Actual $r[0].Text -Message 'the step is described, not just counted'
    Assert-Match -Pattern 'box_a' -Actual $r[0].Text
}

Test 'redo re-applies the undone step' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 4 5 6' | Out-Null
    SendOk 'undo' | Out-Null
    Assert-Vector3Near -Expected @{ x = -3.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'redo' | Out-Null
    Assert-Vector3Near -Expected @{ x = 4.0; y = 5.0; z = 6.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'undo' | Out-Null
}

Test 'the history is LIFO across different kinds of edit' {
    SendOk 'select box_b' | Out-Null
    SendOk 'set_position 1 1 1' | Out-Null          # 1
    SendOk 'create_group Stack' | Out-Null          # 2
    SendOk 'set_group box_b Stack' | Out-Null       # 3
    SendOk 'rename box_b box_stacked' | Out-Null    # 4

    SendOk 'undo' | Out-Null                        # undoes the rename
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value 'box_b' -Message 'rename undone first'
    SendOk 'undo' | Out-Null                        # undoes the group move
    $line = (SendOk 'list_entities')[0].Payload | Where-Object { $_ -like 'box_b *' }
    Assert-NotMatch -Pattern 'group=Stack' -Actual $line -Message 'group move undone second'
    SendOk 'undo' | Out-Null                        # undoes the group creation
    SendOk 'undo' | Out-Null                        # undoes the transform
    Assert-Vector3Near -Expected @{ x = 3.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_b') -Message 'transform undone last'
}

Test 'a new edit after an undo drops the redo stack' {
    SendOk 'select box_c' | Out-Null
    SendOk 'set_position 7 7 7' | Out-Null
    SendOk 'undo' | Out-Null
    SendOk 'set_position 8 8 8' | Out-Null
    Assert-Err -Result (Send 'redo')[0] -Message 'the undone branch is gone'
    SendOk 'undo' | Out-Null
}

Test 'place records one step, and undoing it despawns the instance' {
    $placed = Get-PlacedInstance -Result (Send 'place tf_box')[0]
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value $placed.Name -Message 'after place'
    SendOk 'undo' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value $placed.Name -Message 'after undo'
}

Test 'selection, camera and render settings deliberately do not record' {
    # EditorHistory.h says so explicitly: an undo that only moved the camera back
    # would make the stack useless for the edits it exists for.
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 2 2 2' | Out-Null
    SendOk 'select box_b', 'deselect', 'select box_c' | Out-Null
    SendOk 'camera_pos 30 30 30' | Out-Null
    SendOk 'render aa 0' | Out-Null
    $r = SendOk 'undo'
    Assert-Match -Pattern 'box_a' -Actual $r[0].Text -Message 'the transform is still the top of the stack'
    Assert-Vector3Near -Expected @{ x = -3.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'render aa 1' | Out-Null
}

Test 'the Edit menu drives the same stack' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 11 11 11' | Out-Null
    SendOk 'menu "Edit/Undo"' | Out-Null
    Assert-Vector3Near -Expected @{ x = -3.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'menu "Edit/Redo"' | Out-Null
    Assert-Vector3Near -Expected @{ x = 11.0; y = 11.0; z = 11.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
    SendOk 'menu "Edit/Undo"' | Out-Null
}

Test 'a template edit records too' {
    SendOk 'create_template hist_box' | Out-Null
    $names = @((SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'hist_box' -Message 'after create'
    SendOk 'undo' | Out-Null
    $names = @((SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'hist_box' -Message 'after undo'
}
