# fixture: empty
# description: The engine's built-in shapes - a Mesh is a cube or a plane without importing anything: what the picker offers, switching between the two, the geometry each actually draws, the Add/Plane Object preset, undo, and save/reload.

$Cube = '__default_mesh'
$Plane = '__default_plane'
# Both shapes are assembled out of quads that carry their own frame, which is why the
# cube is 24 vertices rather than 8 (a shared corner would have to pick one face's
# normal) - so 6 faces are 36 indices and the plane, being one face, is 6.
$CubeIndices = 36
$PlaneIndices = 6

function Get-MeshName {
    param([string]$Entity)
    return (Get-Component -Session $Session -Entity $Entity -Component 'Mesh').name
}

# The index count the entity is drawn with - the first of the three DrawIndexed
# arguments, so this says what reached the draw call and not merely what the Mesh
# component claims. `lod_info` reports the selection, hence the select.
function Get-IndexCount {
    param([string]$Entity, $EditorSession)
    if ($null -eq $EditorSession) { $EditorSession = $Session }
    $sel = Invoke-EditorCommand -Session $EditorSession -Command "select $Entity"
    Assert-Ok -Result $sel[0]
    $info = Invoke-EditorCommand -Session $EditorSession -Command 'lod_info'
    Assert-Ok -Result $info[0]
    $line = $info[0].Payload | Where-Object { $_ -like "$Entity *" }
    if ($line -notmatch 'index_count=(\d+)') {
        throw "no index_count for $Entity in: $line"
    }
    return [int]$Matches[1]
}

function Set-Shape {
    param([string]$Entity, [string]$Shape)
    SendOk "set_component $Entity Mesh ""{'name':'$Shape'}""" | Out-Null
    # The box is re-measured and the draw arguments rewritten on background ticks that
    # do not line up with the command, so anything asserted on the geometry waits.
    Step-EditorFrames -Session $Session -Count 4
}

Test 'the built-in shapes lead the mesh picker, and say they are built in' {
    # What the Components and Templates panels' Mesh pickers are built from. The
    # shapes come first and are always offered: the engine assembles them on demand,
    # so listing one is not a claim that the level imported anything - which is why
    # they are listed from World::BuiltinShapes rather than from the loaded meshes.
    $payload = (SendOk 'list_meshes')[0].Payload
    Assert-Match -Pattern "^$Cube\b.*builtin=Cube" -Actual $payload[0] -Message 'the cube leads'
    Assert-Match -Pattern "^$Plane\b.*builtin=Plane" -Actual $payload[1] -Message 'the plane follows it'
}

Test 'a Mesh added from scratch is still the cube' {
    SendOk 'menu "Add/Entity"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    SendOk "add_component $name Mesh", "add_component $name Bounds",
           "add_component $name Material" | Out-Null
    Assert-Equal -Expected $Cube -Actual (Get-MeshName $name) -Message 'the default shape'
    Assert-Equal -Expected $CubeIndices -Actual (Get-IndexCount $name) -Message 'drawn as a cube'
}

Test 'a Mesh can be pointed at the plane, and that geometry is what is drawn' {
    Set-Shape 'box_a' $Plane
    Assert-Equal -Expected $Plane -Actual (Get-MeshName 'box_a') -Message 'the component resolved it'
    # The shape is built on demand, so this is also the assertion that naming one is
    # enough: nothing imported it and no level section declares it.
    Assert-Equal -Expected $PlaneIndices -Actual (Get-IndexCount 'box_a') -Message 'drawn as a quad'
    Set-Shape 'box_a' $Cube
    Assert-Equal -Expected $CubeIndices -Actual (Get-IndexCount 'box_a') -Message 'and back again'
}

Test 'the plane measures flat, which is what a collider is then sized from' {
    # Bounds is a readout: StaticMeshSystem re-measures local_box from the mesh. A
    # plane has no thickness, so its Y extent is zero - legal for a box and not for a
    # collision shape, which is what Physics::AddCollider's MIN_EXTENT clamp is for.
    Set-Shape 'box_a' $Plane
    $bounds = Get-Component -Session $Session -Entity 'box_a' -Component 'Bounds'
    Assert-Near -Expected 0.5 -Actual $bounds.extents.x -Tolerance 0.01 -Message 'half a unit across'
    Assert-Near -Expected 0.5 -Actual $bounds.extents.z -Tolerance 0.01 -Message 'and along'
    Assert-Near -Expected 0.0 -Actual $bounds.extents.y -Tolerance 0.01 -Message 'and no thickness'
    Set-Shape 'box_a' $Cube
    $box = Get-Component -Session $Session -Entity 'box_a' -Component 'Bounds'
    Assert-Near -Expected 0.5 -Actual $box.extents.y -Tolerance 0.01 -Message 'the cube has thickness'
}

Test 'switching shape is one undo step' {
    Set-Shape 'box_a' $Cube
    Set-Shape 'box_a' $Plane
    SendOk 'undo' | Out-Null
    Step-EditorFrames -Session $Session -Count 4
    Assert-Equal -Expected $Cube -Actual (Get-MeshName 'box_a') -Message 'after undo'
    Assert-Equal -Expected $CubeIndices -Actual (Get-IndexCount 'box_a') -Message 'the draw call followed'
    SendOk 'redo' | Out-Null
    Step-EditorFrames -Session $Session -Count 4
    Assert-Equal -Expected $Plane -Actual (Get-MeshName 'box_a') -Message 'after redo'
    Set-Shape 'box_a' $Cube
}

Test 'Add/Plane Object arrives on the plane, ready to draw' {
    SendOk 'menu "Add/Plane Object"' | Out-Null
    $name = (Get-State -Session $Session).selected_entity_name
    Assert-Equal -Expected 'Plane' -Actual $name -Message 'named after its shape'
    $components = (SendOk "components $name")[0].Payload
    foreach ($c in @('Base', 'Transform', 'Mesh', 'Material', 'Bounds')) {
        Assert-Contains -Collection $components -Value $c -Message 'plane object components'
    }
    Assert-Equal -Expected $Plane -Actual (Get-MeshName $name) -Message 'the preset names the shape'
    Assert-Equal -Expected $PlaneIndices -Actual (Get-IndexCount $name) -Message 'drawn as a quad'
}

Test 'a plane reaches the frame' {
    # A shape that is registered, measured and drawn with six indices can still be
    # absent from the image (facing away, zero area, never bound), and nothing above
    # would notice. So: a big plane under the camera, shot twice for the floor - the
    # engine accumulates temporally, so two shots of one state never match exactly -
    # then removed, and the frame has to change by far more than that floor.
    SendOk 'menu "Add/Plane Object"' | Out-Null
    $plane = (Get-State -Session $Session).selected_entity_name
    SendOk "select $plane", 'set_position 0 0 0', 'set_scale 40 1 40',
           'camera_pos 0 14 -18', 'camera_target 0 0 0' | Out-Null
    Start-Sleep -Milliseconds 400
    $want = @(0.0, 14.0, -18.0)
    $arrived = $false
    for ($i = 0; $i -lt 40 -and -not $arrived; $i += 2) {
        $cam = Get-Camera -Session $Session
        # The rendered pose, not the rig's: a commanded camera pose can be lost rather
        # than merely late (see tests\README.md), and then every shot below is of the
        # level's default view - which still contains the boxes, so the comparison
        # quietly becomes one between two pictures of something else.
        $d = 0.0
        for ($k = 0; $k -lt 3; $k++) { $d += [Math]::Pow($cam.world_position[$k] - $want[$k], 2) }
        $arrived = ([Math]::Sqrt($d) -le 0.2)
        if (-not $arrived) { Step-EditorFrames -Session $Session -Count 2 }
    }
    if (-not $arrived) {
        $cam = Get-Camera -Session $Session
        throw "camera never reached 0 14 -18 (rendered at $($cam.world_position -join ', '))"
    }
    Step-EditorFrames -Session $Session -Count 40
    $a = Shot 'plane-drawn-a'
    Step-EditorFrames -Session $Session -Count 4
    $b = Shot 'plane-drawn-b'
    $floor = (Get-ImageDifference -PathA $a -PathB $b -Left 0.3 -Top 0.5 -Right 0.7 -Bottom 0.9 -Step 2).DifferingShare

    SendOk "select $plane", 'delete' | Out-Null
    Step-EditorFrames -Session $Session -Count 40
    $c = Shot 'plane-removed'
    $gone = (Get-ImageDifference -PathA $b -PathB $c -Left 0.3 -Top 0.5 -Right 0.7 -Bottom 0.9 -Step 2).DifferingShare

    Assert-True -Condition ($gone -gt 0.5) `
        -Message "removing the plane changed $([Math]::Round($gone * 100, 1))% of the view below the horizon (floor $([Math]::Round($floor * 100, 1))%)"
    Assert-True -Condition ($gone -gt $floor * 5) `
        -Message "the change is well clear of the floor (gone $gone, floor $floor)"
}

Test 'a shape survives a save and reload' {
    # The level records the shape by name like any other mesh asset, and nothing
    # writes the geometry: Mesh::FromJson has to recognize the name on the next load
    # and build it, or the entity comes back on the default cube with a console line
    # nobody reads.
    Set-Shape 'box_b' $Plane
    SendOk 'menu "File/Save Level"' | Out-Null
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-shapes'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $mesh = Get-Component -Session $reloaded -Entity 'box_b' -Component 'Mesh'
        Assert-Equal -Expected $Plane -Actual $mesh.name -Message 'reloaded on the plane'
        Assert-Equal -Expected $PlaneIndices `
            -Actual (Get-IndexCount 'box_b' -EditorSession $reloaded) `
            -Message 'and the geometry was rebuilt, not defaulted to the cube'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
