# fixture: empty
# description: Entity groups - creation, membership, group selection, persistence in the level file.

Test 'a fresh level has no groups' {
    $r = SendOk 'list_groups'
    Assert-Equal -Expected '0 groups' -Actual $r[0].Text
}

Test 'create_group makes an empty group' {
    $r = SendOk 'create_group Props'
    Assert-Equal -Expected 'group created: Props' -Actual $r[0].Text
    $r = SendOk 'list_groups'
    Assert-Equal -Expected '1 groups' -Actual $r[0].Text
    Assert-Match -Pattern '^Props:' -Actual $r[0].Payload[0]
}

Test 'set_group moves an entity in, and list_entities shows it' {
    SendOk 'set_group box_a Props' | Out-Null
    $r = SendOk 'list_groups'
    Assert-Match -Pattern 'box_a' -Actual ($r[0].Payload -join "`n")
    $line = (SendOk 'list_entities')[0].Payload | Where-Object { $_ -like 'box_a *' }
    Assert-Match -Pattern 'group=Props' -Actual $line
}

Test 'set_group to an unknown group creates it implicitly' {
    SendOk 'set_group box_b Scenery' | Out-Null
    $r = SendOk 'list_groups'
    Assert-Equal -Expected '2 groups' -Actual $r[0].Text
}

Test 'set_group none ungroups' {
    SendOk 'set_group box_b none' | Out-Null
    $line = (SendOk 'list_entities')[0].Payload | Where-Object { $_ -like 'box_b *' }
    Assert-NotMatch -Pattern 'group=' -Actual $line
}

Test 'select_group selects every member' {
    SendOk 'set_group box_b Props', 'set_group box_c Props' | Out-Null
    $r = SendOk 'select_group Props'
    Assert-Equal -Expected '3 selected' -Actual $r[0].Text
    $names = (SendOk 'list_selection')[0].Payload | ForEach-Object { $_ -replace ' \[root\]$', '' }
    foreach ($n in @('box_a', 'box_b', 'box_c')) {
        Assert-Contains -Collection $names -Value $n -Message 'group selection'
    }
}

Test 'select_group add extends rather than replaces' {
    SendOk 'set_group box_c none' | Out-Null
    SendOk 'select box_c' | Out-Null
    SendOk 'select_group Props add' | Out-Null
    Assert-Equal -Expected 3 -Actual (Get-State -Session $Session).selected_count -Message 'box_c plus the two members'
}

Test 'select_group on an unknown group is an error' {
    Assert-Err -Result (Send 'select_group Nope')[0] -Pattern 'unknown group'
}

Test 'set_group on an unknown entity is an error' {
    Assert-Err -Result (Send 'set_group nope Props')[0]
}

Test 'groups persist in the level file' {
    SendOk 'set_group box_c Props' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    Assert-True -Condition ($null -ne $level.editor.groups.Props) -Message 'editor.groups.Props is written'
    foreach ($n in @('box_a', 'box_b', 'box_c')) {
        Assert-Contains -Collection @($level.editor.groups.Props) -Value $n -Message 'saved group members'
    }
}

Test 'a group edit is undoable' {
    SendOk 'set_group box_a none' | Out-Null
    $line = (SendOk 'list_entities')[0].Payload | Where-Object { $_ -like 'box_a *' }
    Assert-NotMatch -Pattern 'group=' -Actual $line
    SendOk 'undo' | Out-Null
    $line = (SendOk 'list_entities')[0].Payload | Where-Object { $_ -like 'box_a *' }
    Assert-Match -Pattern 'group=Props' -Actual $line
}
