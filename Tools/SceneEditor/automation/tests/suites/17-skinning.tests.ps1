# fixture: models
# description: Skinned meshes - clip-measured bounds, colliders fitted to them, bone sockets, normal smoothing.

function BoundsSize {
    param([string]$Entity)
    $b = Get-Component -Session $Session -Entity $Entity -Component 'Bounds'
    return [pscustomobject]@{
        X = 2.0 * $b.extents.x; Y = 2.0 * $b.extents.y; Z = 2.0 * $b.extents.z
        CX = $b.center.x; CY = $b.center.y; CZ = $b.center.z
    }
}

# The collider's world AABB out of physics_info, for the selected entity.
function ColliderAabb {
    $r = SendOk 'physics_info'
    $line = $r[0].Payload | Where-Object { $_ -like '*collider_aabb=*' } | Select-Object -First 1
    if ($line -notmatch 'collider_aabb=\((.+?)\)\.\.\((.+?)\)') { throw "no collider_aabb in: $line" }
    $min = $Matches[1] -split ','
    $max = $Matches[2] -split ','
    return [pscustomobject]@{
        Width  = [double]$max[0] - [double]$min[0]
        Height = [double]$max[1] - [double]$min[1]
        Depth  = [double]$max[2] - [double]$min[2]
        CX = ([double]$max[0] + [double]$min[0]) / 2.0
        CY = ([double]$max[1] + [double]$min[1]) / 2.0
        CZ = ([double]$max[2] + [double]$min[2]) / 2.0
    }
}

Test 'a skinned mesh is measured from the clip it plays, not from the bind pose' {
    # Mesh::GetLocalBox returns the union over the played clip's keyframes. The
    # stored vertex min/max is the space *before* skinning: for a T-posed rig it is
    # several times the model's on-screen size, and Bounds sizes the collider.
    $placed = Get-PlacedInstance -Result (Send 'place tf_troll')[0]
    $script:troll = $placed.Name
    Step-EditorFrames -Session $Session -Count 3

    $idle = BoundsSize -Entity $script:troll
    Assert-True -Condition ($idle.X -gt 0.0 -and $idle.Y -gt 0.0 -and $idle.Z -gt 0.0) `
        -Message 'the box has a size in every axis'
    # The template's 0.025 scale turns the mesh-space box into a model roughly ten
    # world units tall; the T-pose union is twice that.
    $world = [Math]::Max($idle.X, [Math]::Max($idle.Y, $idle.Z)) * 0.025
    Assert-True -Condition ($world -gt 5.0 -and $world -lt 16.0) `
        -Message "the measured box should be model-sized, got $world world units"
}

Test 'the box follows the clip being played' {
    $idle = BoundsSize -Entity $script:troll
    SendOk "set_component $($script:troll) Mesh ""{'animation':'walk'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 5
    $walk = BoundsSize -Entity $script:troll

    $changed = ([Math]::Abs($walk.X - $idle.X) + [Math]::Abs($walk.Y - $idle.Y) +
                [Math]::Abs($walk.Z - $idle.Z) + [Math]::Abs($walk.CX - $idle.CX))
    Assert-True -Condition ($changed -gt 0.0001) `
        -Message 'a different clip sweeps a different volume, so the box is re-measured'
    SendOk "set_component $($script:troll) Mesh ""{'animation':'idle'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
}

Test 'the collider is fitted to that box, not to the stored vertices' {
    SendOk "select $($script:troll)" | Out-Null
    SendOk "set_component $($script:troll) Physics ""{'type':'STATIC','shape':'BOX'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $aabb = ColliderAabb
    Assert-True -Condition ($aabb.Height -gt 5.0 -and $aabb.Height -lt 20.0) `
        -Message "a troll-sized collider, not a bind-pose-sized one (height $($aabb.Height))"
    Assert-True -Condition ($aabb.Height -gt $aabb.Width) -Message 'and standing up'
    # The centre comes from the box too: a collider built from the extents alone
    # spent half its height underground, because the mesh box sits well above the feet.
    Assert-True -Condition ($aabb.CY -gt 1.0) -Message "the collider sits on top of the origin, not around it (centre y $($aabb.CY))"
}

Test 'a CAPSULE runs along the most upright local axis' {
    # MostVerticalAxis, never the longest extent - for a rig the longest extent is
    # as often the arm span as the height.
    SendOk "set_component $($script:troll) Physics ""{'type':'STATIC','shape':'CAPSULE'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $aabb = ColliderAabb
    Assert-True -Condition ($aabb.Height -gt $aabb.Width) `
        -Message 'the capsule stands up rather than lying along the arms'
}

Test 'the collider verdict stays n/a for primitives on a skinned mesh' {
    $verdict = (SendOk 'physics_info')[0].Payload | Where-Object { $_ -like '*collider_smaller_than_mesh_by*' }
    Assert-Match -Pattern 'n/a' -Actual $verdict
}

Test 'a skinned root offers its bones as sockets' {
    SendOk 'create_template socket_rig' | Out-Null
    SendOk 'template_mesh socket_rig troll' | Out-Null
    SendOk 'template_add_animation socket_rig idle troll_idle' | Out-Null
    SendOk ("template_set socket_rig Transform ""{'scale':{'x':0.025,'y':0.025,'z':0.025}," +
            "'rotation':{'x':-0.70710677,'y':0.0,'z':0.0,'w':0.70710677}}""") | Out-Null
    SendOk 'template_add_part socket_rig tf_marker' | Out-Null
    SendOk "template_set_part socket_rig tf_marker ""{'scale':{'x':6.0,'y':6.0,'z':6.0}}""" | Out-Null
    $r = SendOk 'template_parts socket_rig'
    $bones = $r[0].Payload | Where-Object { $_ -like 'bones *' }
    Assert-True -Condition ($null -ne $bones) -Message 'the bones a part could ride are listed'
    Assert-Match -Pattern 'mixamorig:LeftHand' -Actual $bones
    Assert-Match -Pattern 'mixamorig:Hips' -Actual $bones
}

Test 'a part sockets to a bone through Base::parent_bone' {
    SendOk "template_set_part socket_rig tf_marker ""{'bone':'mixamorig:LeftHand'}""" | Out-Null
    $line = (SendOk 'template_parts socket_rig')[0].Payload | Where-Object { $_ -like 'tf_marker *' }
    Assert-Match -Pattern 'attached_to=mixamorig:LeftHand' -Actual $line

    $placed = Get-PlacedInstance -Result (Send 'place socket_rig')[0]
    Step-EditorFrames -Session $Session -Count 5
    $script:socketed = $placed.Name
    $partName = "$($placed.Name)__tf_marker"
    Assert-Contains -Collection (Get-EntityNames -Session $Session) -Value $partName -Message 'the socketed part spawned'

    # Base::parent_bone is the socket: it is the only parenting path that reads the
    # parent's whole world matrix (local * joint * parentWorld), which is what puts
    # a weapon in a hand rather than at the model origin. The part's own Transform
    # stays the *offset from the joint*, not a place in the world.
    $base = Get-Component -Session $Session -Entity $partName -Component 'Base'
    Assert-Equal -Expected $placed.Name -Actual $base.parent -Message 'parented to the root'
    Assert-Equal -Expected 'mixamorig:LeftHand' -Actual $base.parent_bone
    Assert-Vector3Near -Expected @{ x = 0.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity $partName) `
        -Message 'the Transform of an attached part is its offset'
}

Test 'a bone-attached part carries no rigid body, even a static one' {
    # Bone-attached, the pose changes every frame, so a collider is stale the
    # moment it is made - unlike a root-attached static part, which survives.
    SendOk 'template_add_component tf_marker Physics' | Out-Null
    SendOk "template_set tf_marker Physics ""{'type':'STATIC','shape':'BOX'}""" | Out-Null
    $socketed = Get-PlacedInstance -Result (Send 'place socket_rig')[0]
    Assert-NotContains -Collection (SendOk "components $($socketed.Name)__tf_marker")[0].Payload `
        -Value 'Physics' -Message 'components of a socketed part'
}

Test 'clearing the bone puts the part back on the root, static body and all' {
    SendOk "template_set_part socket_rig tf_marker ""{'bone':'','position':{'x':3.0,'y':2.0,'z':0.0}}""" | Out-Null
    $line = (SendOk 'template_parts socket_rig')[0].Payload | Where-Object { $_ -like 'tf_marker *' }
    Assert-Match -Pattern 'attached_to=root' -Actual $line

    $placed = Get-PlacedInstance -Result (Send 'place socket_rig')[0]
    Step-EditorFrames -Session $Session -Count 3
    $partName = "$($placed.Name)__tf_marker"
    Assert-Contains -Collection (SendOk "components $partName")[0].Payload -Value 'Physics' `
        -Message 'a root-attached static part keeps its body'

    # World::ComposedWorldPose seats it: an attached part's Transform is an offset,
    # so a body placed from that Transform alone would sit at the world origin.
    # And the offset is in the root's *own* frame - this root is turned -90 degrees
    # about X to stand the Z-up export upright, so an offset of (3, 2, 0) reaches
    # the world at (3, 0, -2). Its scale, by contrast, does not carry: a part is a
    # whole object with a scale of its own.
    SendOk "select $partName" | Out-Null
    $aabb = ColliderAabb
    Assert-Near -Expected 3.0 -Actual $aabb.CX -Tolerance 0.2 -Message 'collider centre x'
    Assert-Near -Expected 0.0 -Actual $aabb.CY -Tolerance 0.2 -Message 'collider centre y'
    Assert-Near -Expected -2.0 -Actual $aabb.CZ -Tolerance 0.2 -Message 'collider centre z (the offset, turned by the root)'
    SendOk 'template_remove_component tf_marker Physics' | Out-Null
}

Test 'the socket survives a save and reload' {
    SendOk "template_set_part socket_rig tf_marker ""{'bone':'mixamorig:RightHand'}""" | Out-Null
    SendOk 'template_storage socket_rig file' | Out-Null
    SendOk 'save_templates' | Out-Null
    $definition = Get-Content (Join-Path $Assets 'Templates\socket_rig.tpl') -Raw | ConvertFrom-Json
    Assert-Equal -Expected 'mixamorig:RightHand' -Actual $definition.parts[0].bone
    Assert-Equal -Expected 'True' -Actual $definition.parts[0].attach
}

Test 'normal smoothing is a flag on the shared mesh asset' {
    # MeshData keeps both the flat frames and the smooth groups, so the choice is
    # re-derivable rather than baked at import - which is the only reason it can be
    # a component flag at all.
    $mesh = Get-Component -Session $Session -Entity $script:troll -Component 'Mesh'
    $original = $mesh.smooth

    SendOk "set_component $($script:troll) Mesh ""{'smooth':false}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    Assert-Equal -Expected 'False' `
        -Actual (Get-Component -Session $Session -Entity $script:troll -Component 'Mesh').smooth `
        -Message 'an imported mesh has the grouping recorded, so the flag takes'

    # The scope is the asset: every entity drawing that mesh sees it.
    $other = Get-PlacedInstance -Result (Send 'place tf_troll')[0]
    Assert-Equal -Expected 'False' `
        -Actual (Get-Component -Session $Session -Entity $other.Name -Component 'Mesh').smooth `
        -Message 'the flag reaches every user of the mesh'

    SendOk "set_component $($script:troll) Mesh ""{'smooth':$($original.ToString().ToLower())}""" | Out-Null
}

Test 'smoothing changes the shading rather than the geometry' {
    SendOk "select $($script:troll)" | Out-Null
    SendOk 'focus' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $smooth = Shot 'smooth-on'

    SendOk "set_component $($script:troll) Mesh ""{'smooth':false}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $flat = Shot 'smooth-off'

    $diff = Get-ImageDifference -PathA $smooth -PathB $flat
    Assert-True -Condition ($diff.DifferingShare -gt 0.005) `
        -Message "flat shading should look different ($([Math]::Round($diff.DifferingShare * 100, 2))% of pixels differ)"
    SendOk "set_component $($script:troll) Mesh ""{'smooth':true}""" | Out-Null
}

Test 'the smooth flag round-trips through the level file' {
    # Mesh::ToJson writes it unconditionally: undo replays an earlier ToJson, so a
    # key omitted because it matched the import default could never be restored.
    SendOk "set_component $($script:troll) Mesh ""{'smooth':false}""" | Out-Null
    SendOk "rename $($script:troll) flat_troll" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $record = $level.world.entities | Where-Object { $_.name -eq 'flat_troll' }
    Assert-True -Condition ($null -ne $record) -Message 'the entity has a record'
    Assert-Equal -Expected 'False' -Actual $record.components.Mesh.smooth
}
