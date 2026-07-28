# fixture: empty
# description: Colliders - the overlay, physics_info, primitive fits from the local box, and the simulate-physics preview.

function PhysicsLine {
    param([string]$Entity)
    $r = SendOk 'physics_info'
    return ($r[0].Payload | Where-Object { $_ -like "$Entity *" })
}

# The collider's world AABB out of physics_info, as (min, max) triples.
function ColliderAabb {
    param($Payload)
    $line = $Payload | Where-Object { $_ -like '*collider_aabb=*' } | Select-Object -First 1
    if ($line -notmatch 'collider_aabb=\((.+?)\)\.\.\((.+?)\)') { throw "no collider_aabb in: $line" }
    $min = $Matches[1] -split ','
    $max = $Matches[2] -split ','
    return [pscustomobject]@{
        MinX = [double]$min[0]; MinY = [double]$min[1]; MinZ = [double]$min[2]
        MaxX = [double]$max[0]; MaxY = [double]$max[1]; MaxZ = [double]$max[2]
    }
}

Test 'colliders switches the overlay between its three modes' {
    foreach ($mode in @('off', 'selection', 'all')) {
        $r = SendOk "colliders $mode"
        Assert-Equal -Expected "colliders $mode" -Actual $r[0].Text
        Assert-Equal -Expected $mode -Actual (Get-State -Session $Session).collider_view
    }
    Assert-Err -Result (Send 'colliders sometimes')[0] -Pattern 'usage:'
    SendOk 'colliders off' | Out-Null
}

Test 'the View menu drives the same overlay' {
    SendOk 'menu "View/Colliders: All"' | Out-Null
    Assert-Equal -Expected 'all' -Actual (Get-State -Session $Session).collider_view
    SendOk 'menu "View/Colliders: Selection"' | Out-Null
    Assert-Equal -Expected 'selection' -Actual (Get-State -Session $Session).collider_view
    SendOk 'colliders off' | Out-Null
}

Test 'physics_info needs a selection' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'physics_info')[0] -Pattern 'nothing selected'
}

Test 'physics_info reports the live body, shape and both AABBs' {
    SendOk 'select box_a' | Out-Null
    $r = SendOk 'physics_info'
    Assert-Match -Pattern 'body=(static|kinematic|dynamic)' -Actual (PhysicsLine -Entity 'box_a')
    Assert-Match -Pattern 'active=[01]' -Actual (PhysicsLine -Entity 'box_a')
    Assert-Match -Pattern 'entity_scale=\(1,1,1\)' -Actual (PhysicsLine -Entity 'box_a')
    Assert-True -Condition (@($r[0].Payload | Where-Object { $_ -like '*collider_aabb=*' }).Count -eq 1) -Message 'collider AABB'
    Assert-True -Condition (@($r[0].Payload | Where-Object { $_ -like '*mesh_aabb=*' }).Count -eq 1) -Message 'mesh AABB'
}

Test 'a primitive collider reports n/a rather than SUSPECT' {
    # Boxes, spheres and capsules approximate the mesh by design; only a mesh
    # collider is supposed to track it, so flagging the primitives would be noise.
    SendOk 'select box_a' | Out-Null
    SendOk "set_component box_a Physics ""{'type':'STATIC','shape':'SPHERE'}""" | Out-Null
    $r = SendOk 'physics_info'
    $verdict = $r[0].Payload | Where-Object { $_ -like '*collider_smaller_than_mesh_by*' }
    Assert-Match -Pattern 'n/a' -Actual $verdict
    Assert-NotMatch -Pattern 'SUSPECT' -Actual $verdict
}

Test 'a BOX collider is the entity local box, centred where the box is' {
    # Physics::AddCollider builds the half extents *and* the centre out of
    # Bounds::local_box scaled by the Transform - dropping the centre is what put
    # half of a model underground.
    SendOk 'select box_b' | Out-Null
    SendOk 'set_position 0 0 0', 'set_scale 1 1 1', 'set_rotation 0 0 0' | Out-Null
    SendOk "set_component box_b Physics ""{'type':'STATIC','shape':'BOX'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $aabb = ColliderAabb -Payload (SendOk 'physics_info')[0].Payload
    # The built-in cube is the unit cube, so its collider is [-0.5, 0.5] in each axis.
    Assert-Near -Expected -0.5 -Actual $aabb.MinX -Tolerance 0.05 -Message 'min x'
    Assert-Near -Expected 0.5 -Actual $aabb.MaxX -Tolerance 0.05 -Message 'max x'
    Assert-Near -Expected -0.5 -Actual $aabb.MinY -Tolerance 0.05 -Message 'min y'
    Assert-Near -Expected 0.5 -Actual $aabb.MaxY -Tolerance 0.05 -Message 'max y'
}

Test 'the collider follows the entity transform' {
    SendOk 'select box_b' | Out-Null
    SendOk 'set_position 10 4 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $aabb = ColliderAabb -Payload (SendOk 'physics_info')[0].Payload
    Assert-Near -Expected 10.0 -Actual (($aabb.MinX + $aabb.MaxX) / 2.0) -Tolerance 0.05 -Message 'collider centre x'
    Assert-Near -Expected 4.0 -Actual (($aabb.MinY + $aabb.MaxY) / 2.0) -Tolerance 0.05 -Message 'collider centre y'
}

Test 'the collider follows the entity scale' {
    SendOk 'select box_b' | Out-Null
    SendOk 'set_position 0 0 0', 'set_scale 4 2 4' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $aabb = ColliderAabb -Payload (SendOk 'physics_info')[0].Payload
    Assert-Near -Expected 4.0 -Actual ($aabb.MaxX - $aabb.MinX) -Tolerance 0.2 -Message 'scaled x extent'
    Assert-Near -Expected 2.0 -Actual ($aabb.MaxY - $aabb.MinY) -Tolerance 0.2 -Message 'scaled y extent'
    SendOk 'set_scale 1 1 1' | Out-Null
}

Test 'a rotated box collider is not a rotated extents vector' {
    # A box does not rotate by rotating its extents - that mixes axes as soon as
    # the rotation is not a multiple of 90 degrees. The body carries the rotation
    # and the collider's own local rotation is the identity.
    SendOk 'select box_b' | Out-Null
    SendOk 'set_scale 4 1 1', 'set_rotation 0 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $flat = ColliderAabb -Payload (SendOk 'physics_info')[0].Payload
    Assert-Near -Expected 4.0 -Actual ($flat.MaxX - $flat.MinX) -Tolerance 0.2 -Message 'unrotated x extent'

    SendOk 'set_rotation 0 90 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $turned = ColliderAabb -Payload (SendOk 'physics_info')[0].Payload
    Assert-Near -Expected 1.0 -Actual ($turned.MaxX - $turned.MinX) -Tolerance 0.3 `
        -Message 'turned 90 degrees, the long axis is now z'
    Assert-Near -Expected 4.0 -Actual ($turned.MaxZ - $turned.MinZ) -Tolerance 0.3 -Message 'turned z extent'
    SendOk 'set_rotation 0 0 0', 'set_scale 1 1 1' | Out-Null
}

Test 'the body type follows the component' {
    SendOk 'select box_c' | Out-Null
    foreach ($pair in @(@('STATIC', 'static'), @('KINEMATIC', 'kinematic'), @('DYNAMIC', 'dynamic'))) {
        SendOk "set_component box_c Physics ""{'type':'$($pair[0])'}""" | Out-Null
        Assert-Match -Pattern "body=$($pair[1])" -Actual (PhysicsLine -Entity 'box_c') -Message $pair[0]
    }
    SendOk "set_component box_c Physics ""{'type':'STATIC'}""" | Out-Null
}

Test 'an entity without Physics says so instead of failing' {
    SendOk 'remove_component box_c Physics' | Out-Null
    SendOk 'select box_c' | Out-Null
    Assert-Match -Pattern 'no Physics component' -Actual (PhysicsLine -Entity 'box_c')
    SendOk 'add_component box_c Physics' | Out-Null
}

Test 'Simulate Physics is a preview: switching it off rewinds the scene' {
    # PhysicsPreview snapshots every non-static body when it starts and rewinds to
    # those snapshots when it stops.
    SendOk 'select box_a' | Out-Null
    SendOk "set_component box_a Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null
    SendOk 'set_position 0 20 0' | Out-Null
    $before = Get-Position -Session $Session -Entity 'box_a'

    SendOk 'menu "Edit/Simulate Physics"' | Out-Null
    Step-EditorFrames -Session $Session -Count 25
    $falling = Get-Position -Session $Session -Entity 'box_a'
    Assert-True -Condition ($falling.y -lt $before.y) -Message 'a dynamic body falls while the preview runs'

    SendOk 'menu "Edit/Simulate Physics"' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    Assert-Vector3Near -Expected $before -Actual (Get-Position -Session $Session -Entity 'box_a') -Tolerance 0.05 `
        -Message 'switching the preview off rewinds to the authored pose'
}

Test 'physics is paused while editing, so a transform edit stays put' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 0 15 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 20
    Assert-Vector3Near -Expected @{ x = 0.0; y = 15.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a') -Tolerance 0.001 `
        -Message 'a dynamic body holds the pose it was authored at'
}

Test 'the collider overlay renders without disturbing the scene' {
    SendOk 'colliders off' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $plain = Shot 'colliders-off'
    SendOk 'colliders all' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $overlay = Shot 'colliders-all'
    $diff = Get-ImageDifference -PathA $plain -PathB $overlay
    Assert-True -Condition ($diff.DifferingShare -gt 0.001) -Message 'the wireframe is drawn'
    SendOk 'colliders off' | Out-Null
    Assert-Ok -Result (Send 'ping')[0]
}
