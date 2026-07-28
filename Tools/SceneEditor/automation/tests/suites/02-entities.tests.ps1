# fixture: empty
# description: Entity listing, selection semantics (primary vs root), focus, rename.

Test 'list_entities reports every entity with id and position' {
    $r = SendOk 'list_entities'
    Assert-Equal -Expected '6 entities' -Actual $r[0].Text
    $names = Get-EntityNames -Session $Session
    foreach ($n in @('ambient', 'sun', 'camera_rig', 'box_a', 'box_b', 'box_c')) {
        Assert-Contains -Collection $names -Value $n -Message 'entity list'
    }
    $boxA = $r[0].Payload | Where-Object { $_ -like 'box_a *' }
    Assert-Match -Pattern 'id=\d+' -Actual $boxA
    Assert-Match -Pattern 'pos=\(-3,0,0\)' -Actual $boxA
}

Test 'select replaces the selection and the last pick becomes primary' {
    $r = SendOk 'select box_a box_b box_c'
    Assert-Match -Pattern '^3 selected, primary id=\d+$' -Actual $r[0].Text
    $state = Get-State -Session $Session
    Assert-Equal -Expected 'box_c' -Actual $state.selected_entity_name -Message 'primary is the last pick'
    Assert-Equal -Expected 3 -Actual $state.selected_count
}

Test 'list_selection reports pick order and marks the root' {
    SendOk 'select box_a box_b box_c' | Out-Null
    $r = SendOk 'list_selection'
    Assert-Equal -Expected '3 selected' -Actual $r[0].Text
    Assert-Equal -Expected 'box_a [root]' -Actual $r[0].Payload[0] -Message 'first pick is the root'
    Assert-Equal -Expected 'box_b' -Actual $r[0].Payload[1]
    Assert-Equal -Expected 'box_c' -Actual $r[0].Payload[2]
}

Test 'a single selection is not marked as a root' {
    SendOk 'select box_a' | Out-Null
    $r = SendOk 'list_selection'
    Assert-Equal -Expected 'box_a' -Actual $r[0].Payload[0] -Message 'nothing to be the root of'
}

Test 'add_select extends the selection without moving the earlier picks' {
    SendOk 'select box_c' | Out-Null
    SendOk 'add_select box_a' | Out-Null
    $r = SendOk 'list_selection'
    Assert-Equal -Expected 'box_c [root]' -Actual $r[0].Payload[0]
    Assert-Equal -Expected 'box_a' -Actual $r[0].Payload[1]
    Assert-Equal -Expected 'box_a' -Actual (Get-State -Session $Session).selected_entity_name
}

Test 'select with an unknown name changes nothing' {
    SendOk 'select box_a' | Out-Null
    Assert-Err -Result (Send 'select box_b nope box_c')[0] -Pattern 'entity not found: nope'
    $state = Get-State -Session $Session
    Assert-Equal -Expected 1 -Actual $state.selected_count -Message 'all-or-nothing'
    Assert-Equal -Expected 'box_a' -Actual $state.selected_entity_name
}

Test 'select with no arguments clears the selection' {
    SendOk 'select box_a box_b' | Out-Null
    SendOk 'select' | Out-Null
    Assert-Equal -Expected 0 -Actual (Get-State -Session $Session).selected_count
}

Test 'add_select with no arguments is a usage error' {
    Assert-Err -Result (Send 'add_select')[0] -Pattern 'usage:'
}

Test 'deselect clears the selection' {
    SendOk 'select box_a' | Out-Null
    $r = SendOk 'deselect'
    Assert-Equal -Expected 'nothing selected' -Actual $r[0].Text
    Assert-Equal -Expected 0 -Actual (Get-State -Session $Session).selected_count
}

Test 'list_entities marks the primary and the other selected entities' {
    SendOk 'select box_a box_b' | Out-Null
    $r = SendOk 'list_entities'
    $a = $r[0].Payload | Where-Object { $_ -like 'box_a *' }
    $b = $r[0].Payload | Where-Object { $_ -like 'box_b *' }
    $c = $r[0].Payload | Where-Object { $_ -like 'box_c *' }
    Assert-Match -Pattern '\[also selected\]' -Actual $a
    Assert-Match -Pattern '\[selected\]' -Actual $b
    Assert-NotMatch -Pattern 'selected' -Actual $c
}

Test 'focus moves the camera to frame the selection' {
    SendOk 'camera_pos 60 60 60', 'camera_target 0 0 0' | Out-Null
    SendOk 'select box_a' | Out-Null
    $before = Get-Camera -Session $Session
    $r = SendOk 'focus'
    Assert-Match -Pattern '^focused entity \d+$' -Actual $r[0].Text
    $after = Get-Camera -Session $Session
    # Focus frames the entity: the target lands on it and the camera comes closer
    # than the 104-unit corner it started from.
    Assert-Near -Expected -3.0 -Actual $after.target[0] -Tolerance 0.5 -Message 'target x follows box_a'
    Assert-True -Condition ($after.distance -lt $before.distance) -Message 'framing pulls the camera in'
}

Test 'focus with nothing selected is rejected' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'focus')[0]
}

Test 'rename changes the entity name everywhere' {
    SendOk 'rename box_c box_renamed' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-Contains -Collection $names -Value 'box_renamed' -Message 'entity list'
    Assert-NotContains -Collection $names -Value 'box_c' -Message 'entity list'
    # The new name is the key everything else uses.
    Assert-Ok -Result (Send 'select box_renamed')[0]
    Assert-Err -Result (Send 'select box_c')[0]
    SendOk 'rename box_renamed box_c' | Out-Null
}

Test 'rename rejects an empty, duplicate or unknown name' {
    Assert-Err -Result (Send 'rename box_a box_b')[0] -Message 'duplicate name'
    Assert-Err -Result (Send 'rename nope whatever')[0] -Message 'unknown entity'
    Assert-Err -Result (Send 'rename box_a')[0] -Pattern 'usage:'
}

Test 'components lists an entity in registry order' {
    $r = SendOk 'components box_a'
    Assert-Match -Pattern '^\d+ components on box_a$' -Actual $r[0].Text
    Assert-Equal -Expected 'Base' -Actual $r[0].Payload[0] -Message 'Base is first'
    Assert-Equal -Expected 'Transform' -Actual $r[0].Payload[1] -Message 'Transform is second'
    foreach ($c in @('Mesh', 'Material', 'Bounds')) {
        Assert-Contains -Collection $r[0].Payload -Value $c -Message 'components of a placed cube'
    }
}

Test 'components on an unknown entity is an error' {
    Assert-Err -Result (Send 'components nope')[0] -Pattern 'unknown entity'
}
