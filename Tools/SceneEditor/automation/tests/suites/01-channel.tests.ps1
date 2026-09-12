# fixture: empty
# description: The channel itself - liveness, batching, tokenizer, state dump, menu registry.

Test 'ping answers pong' {
    $r = Send 'ping'
    Assert-Equal -Expected 1 -Actual $r.Count -Message 'one result per command'
    Assert-Ok -Result $r[0]
    Assert-Equal -Expected 'pong' -Actual $r[0].Text
}

Test 'a batch runs every line, in order, in one frame' {
    $r = Send 'ping', 'state', 'ping'
    Assert-Equal -Expected 3 -Actual $r.Count
    Assert-Ok -Result $r
    Assert-Equal -Expected 'ping' -Actual $r[0].Command
    Assert-Equal -Expected 'state' -Actual $r[1].Command
    Assert-Equal -Expected 'ping' -Actual $r[2].Command
}

Test 'an unknown command is rejected without killing the batch' {
    $r = Send 'ping', 'no_such_command', 'ping'
    Assert-Ok -Result $r[0]
    Assert-Err -Result $r[1] -Pattern 'unknown command'
    Assert-Ok -Result $r[2] -Message 'the rest of the batch still runs'
}

Test 'a command missing its arguments answers ERR usage' {
    $r = Send 'select_group', 'rename', 'set_component box_a Base'
    foreach ($result in $r) {
        Assert-Err -Result $result -Pattern 'usage:'
    }
}

Test 'double quotes group an argument containing spaces' {
    # The tokenizer strips quotes and joins the run - which is how menu paths and
    # Windows paths with spaces arrive intact.
    Assert-Ok -Result (Send 'menu "Edit/Gizmo: Rotate"')[0]
    Assert-Err -Result (Send 'menu Edit/Gizmo: Rotate')[0] -Pattern 'unknown menu command' `
        -Message 'unquoted, only "Edit/Gizmo:" arrives as the argument'
    SendOk 'menu "Edit/Gizmo: Translate"' | Out-Null
}

Test 'state reports the loaded level and project root' {
    $state = Get-State -Session $Session
    Assert-True -Condition $state.level_loaded -Message 'level_loaded'
    Assert-Equal -Expected $LevelPath -Actual $state.level_path
    Assert-Equal -Expected ([IO.Path]::GetFullPath($Project)) -Actual $state.project_root
}

Test 'state counts the fixture: 6 entities, 3 templates, 4 placed instances' {
    $state = Get-State -Session $Session
    Assert-Equal -Expected 6 -Actual $state.entity_count -Message 'ambient, sun, camera_rig, 3 boxes'
    Assert-Equal -Expected 3 -Actual $state.templates.Count
    Assert-Equal -Expected 4 -Actual $state.placed_instances
    Assert-Equal -Expected 0 -Actual $state.unsaved_templates
}

Test 'state tracks the selection' {
    SendOk 'select box_a' | Out-Null
    $state = Get-State -Session $Session
    Assert-Equal -Expected 1 -Actual $state.selected_count
    Assert-Equal -Expected 'box_a' -Actual $state.selected_entity_name
    Assert-NotEqual -Expected -1 -Actual $state.selected_entity

    SendOk 'deselect' | Out-Null
    $state = Get-State -Session $Session
    Assert-Equal -Expected 0 -Actual $state.selected_count
    Assert-Equal -Expected -1 -Actual $state.selected_entity
}

Test 'state reports the gizmo mode, and the Edit menu switches it' {
    $state = Get-State -Session $Session
    Assert-Equal -Expected 'translate' -Actual $state.gizmo_mode
    SendOk 'menu "Edit/Gizmo: Rotate"' | Out-Null
    Assert-Equal -Expected 'rotate' -Actual (Get-State -Session $Session).gizmo_mode
    SendOk 'menu "Edit/Gizmo: Scale"' | Out-Null
    Assert-Equal -Expected 'scale' -Actual (Get-State -Session $Session).gizmo_mode
    SendOk 'menu "Edit/Gizmo: Translate"' | Out-Null
    Assert-Equal -Expected 'translate' -Actual (Get-State -Session $Session).gizmo_mode
}

Test 'menus lists the registry, and every path in it executes' {
    $r = SendOk 'menus'
    Assert-Match -Pattern '^\d+ menu commands$' -Actual $r[0].Text
    $count = [int]($r[0].Text -split ' ')[0]
    Assert-Equal -Expected $count -Actual $r[0].Payload.Count -Message 'one line per command'
    # The registry is what makes a menu entry scriptable at all; these are the
    # ones the suites below drive, so a rename in SceneEditor.cpp fails here first.
    $paths = @($r[0].Payload | ForEach-Object { ($_ -replace ' \(disabled\)$', '') })
    foreach ($expected in @('File/Save Level', 'File/Save Materials', 'File/Save Templates',
                            'Edit/Undo', 'Edit/Redo', 'Edit/Copy', 'Edit/Cut', 'Edit/Paste',
                            'Edit/Delete', 'Edit/Simulate Physics',
                            'Edit/Gizmo: Translate', 'Edit/Gizmo: Rotate', 'Edit/Gizmo: Scale',
                            'Edit/Create Template from Selection', 'Edit/Apply Instance to Template',
                            'Add/Entity',
                            'View/Entities', 'View/Components', 'View/Asset Browser',
                            'View/Materials', 'View/Templates', 'View/Colliders: Selection',
                            'View/Colliders: All', 'View/Shadow Cascades', 'View/Static Shadow Map',
                            'View/Reset Layout')) {
        Assert-Contains -Collection $paths -Value $expected -Message 'menu registry'
    }
}

Test 'an unknown menu path is rejected' {
    Assert-Err -Result (Send 'menu "File/Nope"')[0] -Pattern 'unknown menu command'
}

Test 'open_level with one already open closes it first and reopens fresh' {
    # SceneEditorApp::OpenLevel used to refuse a second load per session; it now
    # closes whatever is open (CloseLevel: destroy and reconstruct the World,
    # mirroring MarblesGame::ExitGame/LoadLevel) and reopens, the same path
    # File/Open Level... drives from the menu while a level is already loaded.
    # Reload the same fixture level, so the counts below double as proof the
    # close+reopen cycle didn't corrupt or drop anything.
    $r = Send "open_level $LevelPath"
    Assert-Ok -Result $r[0]
    Assert-Match -Pattern '^Loaded: ' -Actual $r[0].Text

    $state = Get-State -Session $Session
    Assert-True -Condition $state.level_loaded -Message 'level_loaded after reopening'
    Assert-Equal -Expected $LevelPath -Actual $state.level_path
    Assert-Equal -Expected 6 -Actual $state.entity_count -Message 'the fixture reloads with the same content'
    Assert-Equal -Expected 3 -Actual $state.templates.Count
    Assert-Equal -Expected 4 -Actual $state.placed_instances
    # A fresh World means a fresh EditorState too - selection and gizmo mode
    # (set earlier in this suite) are back to their just-opened defaults.
    Assert-Equal -Expected 0 -Actual $state.selected_count -Message 'reopening starts with a clean selection'
    Assert-Equal -Expected 'translate' -Actual $state.gizmo_mode
}

Test 'screenshot writes a PNG of the frame' {
    $path = Shot 'channel-frame'
    Assert-FileExists -Path $path -Message 'screenshot'
    $stats = Get-ImageStats -Path $path
    Assert-True -Condition ($stats.Width -gt 0 -and $stats.Height -gt 0) -Message 'PNG has dimensions'
}
