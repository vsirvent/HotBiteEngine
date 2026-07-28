# fixture: empty
# description: Component add/remove/read/edit through ComponentOps, and the per-entity deltas they write.

Test 'component prints the serialized state the panel edits' {
    $base = Get-Component -Session $Session -Entity 'box_a' -Component 'Base'
    foreach ($field in @('visible', 'scene_visible', 'cast_shadow', 'draw_depth', 'is_static', 'pass', 'draw_method')) {
        Assert-True -Condition ($null -ne $base.$field) -Message "Base.$field is serialized"
    }
    Assert-Equal -Expected 'True' -Actual $base.visible
    Assert-Equal -Expected 'False' -Actual $base.is_static
}

Test 'component on a component the entity does not have is an error' {
    Assert-Err -Result (Send 'component box_a Camera')[0] -Pattern 'has no Camera'
    Assert-Err -Result (Send 'component box_a Nonsense')[0]
}

Test 'a placed instance carries the components its template declares' {
    # tf_box declares nothing, so this is what World gives every spawned mesh
    # entity: the draw set plus a body. A change here is a change to what "place"
    # means, which is worth failing a test over.
    $components = (SendOk 'components box_a')[0].Payload
    foreach ($c in @('Base', 'Transform', 'Physics', 'Mesh', 'Material', 'Bounds', 'Lighted')) {
        Assert-Contains -Collection $components -Value $c -Message 'components of a placed cube'
    }
    Assert-Contains -Collection (SendOk 'components camera_rig')[0].Payload -Value 'Camera' `
        -Message 'the template Camera block reached the instance'
}

Test 'set_component edits named fields and leaves the rest alone' {
    SendOk "set_component box_a Base ""{'cast_shadow':false,'pass':2}""" | Out-Null
    $base = Get-Component -Session $Session -Entity 'box_a' -Component 'Base'
    Assert-Equal -Expected 'False' -Actual $base.cast_shadow
    Assert-Equal -Expected 2 -Actual $base.pass
    Assert-Equal -Expected 'True' -Actual $base.visible -Message 'a key left out keeps its value'
    SendOk "set_component box_a Base ""{'cast_shadow':true,'pass':1}""" | Out-Null
}

Test 'set_component rejects malformed JSON and non-objects' {
    Assert-Err -Result (Send "set_component box_a Base ""{'visible':}""")[0] -Pattern 'bad JSON'
    Assert-Err -Result (Send "set_component box_a Base ""[1,2]""")[0] -Pattern 'must be a JSON object'
}

Test 'set_component on a component the entity lacks is an error' {
    Assert-Err -Result (Send "set_component box_a Camera ""{'position':{'x':1.0}}""")[0]
}

Test 'remove_component takes a component away and add_component puts one back' {
    Assert-Contains -Collection (SendOk 'components box_b')[0].Payload -Value 'Physics' -Message 'before'
    SendOk 'remove_component box_b Physics' | Out-Null
    Assert-NotContains -Collection (SendOk 'components box_b')[0].Payload -Value 'Physics' -Message 'after remove'

    SendOk 'add_component box_b Physics' | Out-Null
    Assert-Contains -Collection (SendOk 'components box_b')[0].Payload -Value 'Physics' -Message 'after add'
    $physics = Get-Component -Session $Session -Entity 'box_b' -Component 'Physics'
    Assert-True -Condition ($null -ne $physics.type) -Message 'an added Physics serializes a type'
}

Test 'add_component refuses a duplicate, an unknown name, and one needing an asset' {
    Assert-Err -Result (Send 'add_component box_a Base')[0] -Message 'already present'
    Assert-Err -Result (Send 'add_component box_a Nonsense')[0] -Message 'not a registered component'
    # Mesh/Material/Bounds/Lighted cannot be default-constructed - they need an
    # asset picked first, which is a panel interaction rather than a command.
    Assert-Err -Result (Send 'add_component camera_rig Mesh')[0] -Message 'needs an asset'
}

Test 'remove_component refuses the required and the engine-managed components' {
    Assert-Err -Result (Send 'remove_component box_a Base')[0] -Message 'Base is required'
    Assert-Err -Result (Send 'remove_component box_a Transform')[0] -Message 'Transform is required'
    Assert-Err -Result (Send 'remove_component camera_rig Camera')[0] -Message 'Camera is engine-managed'
}

Test 'remove_component undoes back to the values it had, not to defaults' {
    SendOk "set_component box_c Physics ""{'type':'STATIC','shape':'SPHERE'}""" | Out-Null
    $before = Get-Component -Session $Session -Entity 'box_c' -Component 'Physics'
    Assert-Equal -Expected 'STATIC' -Actual $before.type
    Assert-Equal -Expected 'SPHERE' -Actual $before.shape

    SendOk 'remove_component box_c Physics' | Out-Null
    SendOk 'undo' | Out-Null
    $after = Get-Component -Session $Session -Entity 'box_c' -Component 'Physics'
    Assert-Equal -Expected 'STATIC' -Actual $after.type -Message 'restored value, not the default'
    Assert-Equal -Expected 'SPHERE' -Actual $after.shape
    SendOk 'remove_component box_c Physics' | Out-Null
}

Test 'Physics rebuilds its rigid body when type or shape changes' {
    # A setter alone would leave the component describing a body that does not
    # match; Physics::FromJson rebuilds, and physics_info reads the live body.
    SendOk "set_component box_b Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null
    SendOk 'select box_b' | Out-Null
    $r = SendOk 'physics_info'
    $line = $r[0].Payload | Where-Object { $_ -like 'box_b *' }
    Assert-Match -Pattern 'body=dynamic' -Actual $line

    SendOk "set_component box_b Physics ""{'type':'STATIC'}""" | Out-Null
    $r = SendOk 'physics_info'
    $line = $r[0].Payload | Where-Object { $_ -like 'box_b *' }
    Assert-Match -Pattern 'body=static' -Actual $line -Message 'the live body followed the edit'
}

Test 'a component edit is written to that entity, not to its template' {
    # ComponentOps writes into EditorState::component_deltas, which SceneSerializer
    # turns into the entity's own "components" record.
    SendOk "set_component box_a Base ""{'pass':3}""" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $record = $level.world.entities | Where-Object { $_.name -eq 'box_a' }
    Assert-True -Condition ($null -ne $record) -Message 'box_a has an entities record'
    Assert-Equal -Expected 3 -Actual $record.components.Base.pass
    $sibling = $level.world.entities | Where-Object { $_.name -eq 'box_b' }
    if ($null -ne $sibling -and $null -ne $sibling.components.Base) {
        Assert-NotEqual -Expected 3 -Actual $sibling.components.Base.pass -Message 'the sibling instance is untouched'
    }
    SendOk "set_component box_a Base ""{'pass':1}""" | Out-Null
}

Test 'Mesh always serializes the smooth flag' {
    # ToJson writes "smooth" unconditionally rather than only when it differs from
    # the import default: undo works by replaying an earlier ToJson, so a key
    # omitted because it matched the default could never be restored.
    $mesh = Get-Component -Session $Session -Entity 'box_a' -Component 'Mesh'
    Assert-True -Condition ($null -ne $mesh.smooth) -Message 'Mesh serializes "smooth"'
    Assert-Equal -Expected '__default_mesh' -Actual $mesh.name
}

Test 'smoothing the built-in cube reports no change rather than a false one' {
    # MeshData::SetSmooth refuses when the mesh carries no recorded grouping - the
    # generated cube has none - and leaves the readout honest about what is on
    # screen. (An imported mesh is the interesting case; see the models suite.)
    SendOk "set_component box_a Mesh ""{'smooth':false}""" | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Mesh').smooth
}

Test 'Bounds is a readout measured from the mesh' {
    $bounds = Get-Component -Session $Session -Entity 'box_a' -Component 'Bounds'
    # The built-in cube is a unit cube centred on the origin.
    Assert-Vector3Near -Expected @{ x = 0.5; y = 0.5; z = 0.5 } -Actual $bounds.extents -Tolerance 0.01
    Assert-Vector3Near -Expected @{ x = 0.0; y = 0.0; z = 0.0 } -Actual $bounds.center -Tolerance 0.01
}
