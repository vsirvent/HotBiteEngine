# fixture: lods
# description: Levels of detail - the chain on the mesh asset, and the renderer switching between its levels by screen area and by distance.

# The fixture's two unskinned meshes. 'Space' is the full-detail one and 'Sky' the
# coarse stand-in; the reduction is exact and is what every ratio assertion below
# is checking the engine derived rather than guessed.
$FullVertices = 2880
$CoarseVertices = 36
$CoarseRatio = $CoarseVertices / $FullVertices   # 0.0125

# The parsed `lod_info` line for one entity, plus its levels.
#
# It has to be its own batch, and separate from whatever moved the camera. The
# channel runs a batch before the frame and the selection happens during it
# (RenderSystem::SelectLods, once per frame), so a camera move and a lod_info in
# one batch report the level the *previous* frame was drawn at - which reads as
# the switch not happening at all.
function Get-Lod {
    param([string]$Entity)
    SendOk "select $Entity" | Out-Null
    $r = SendOk 'lod_info'
    $line = $r[0].Payload | Where-Object { $_ -like "$Entity *" }
    if (-not $line) { throw "lod_info reported nothing for $Entity" }
    # Named 'Chain' and not 'Levels': PowerShell hashtable keys are case
    # insensitive, so it would be the same key as the 'levels' field parsed below
    # and the count and the list would overwrite each other.
    $info = @{ Chain = @() }
    foreach ($field in @('mode', 'bias', 'enabled', 'levels', 'current',
                         'index_count', 'index_offset', 'vertex_offset')) {
        $m = [regex]::Match($line, "\b$field=(\S+)")
        if (-not $m.Success) { throw "lod_info line has no $field`: $line" }
        $info[$field] = $m.Groups[1].Value
    }
    foreach ($l in ($r[0].Payload | Where-Object { $_ -match '^\s+lod\d' })) {
        $info.Chain += @{
            Mesh     = [regex]::Match($l, 'mesh=(\S+)').Groups[1].Value
            Ratio    = [double][regex]::Match($l, 'ratio=(\S+)').Groups[1].Value
            Distance = [double][regex]::Match($l, 'distance=(\S+)').Groups[1].Value
            Vertices = [int][regex]::Match($l, 'vertices=(\S+)').Groups[1].Value
        }
    }
    return $info
}

# Puts the camera `Distance` from the origin, looking at it, and lets the frame
# that draws from there happen before anything reads the result.
function Set-CameraDistance {
    param([double]$Distance)
    SendOk 'camera_target 0 0 0' | Out-Null
    SendOk "camera_pos 0 0 -$Distance" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
}

function Set-Chain {
    param([string]$Entity, [string]$Json)
    SendOk "set_component $Entity Mesh ""$Json""" | Out-Null
}

#--- the chain ----------------------------------------------------------------

Test 'a mesh declares no levels until something gives it some' {
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '0' -Actual $lod.levels -Message 'levels'
    Assert-Equal -Expected '0' -Actual $lod.current -Message 'current level'
    # "No chain" is what every mesh in every existing level looks like, so this is
    # also the assertion that the feature costs those levels nothing.
    Assert-Equal -Expected $FullVertices -Actual ([int]$lod.index_count) -Message 'draws the full mesh'
}

Test 'lods installs a chain and derives each level ratio from its vertex count' {
    Set-Chain 'dome_a' "{'lods':['Sky']}"
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'levels'
    # Level 0 is the mesh itself, always, so the chain reads the same way at
    # every index.
    Assert-Equal -Expected 'Space' -Actual $lod.Chain[0].Mesh -Message 'level 0 is the mesh'
    Assert-Near -Expected 1.0 -Actual $lod.Chain[0].Ratio -Message 'level 0 ratio'
    Assert-Equal -Expected 'Sky' -Actual $lod.Chain[1].Mesh -Message 'level 1 mesh'
    Assert-Near -Expected $CoarseRatio -Actual $lod.Chain[1].Ratio -Tolerance 0.0001 `
        -Message 'the reduction is derived, not authored'
    Assert-Equal -Expected $CoarseVertices -Actual $lod.Chain[1].Vertices -Message 'level 1 vertices'
}

Test 'the chain belongs to the mesh asset, so every entity drawing it has one' {
    # dome_b was never edited: it shares the MeshData, exactly as it shares the
    # smoothing flag and the animation sets.
    $lod = Get-Lod 'dome_b'
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'levels on the second entity'
    Assert-Equal -Expected 'Sky' -Actual $lod.Chain[1].Mesh -Message 'the same level 1'
}

Test 'a level that cannot stand in for the mesh is refused, and the rest survive' {
    # The mesh itself, and a name no mesh has. Both are dropped and 'Sky' is kept,
    # rather than the whole chain being lost to one bad entry.
    Set-Chain 'dome_a' "{'lods':['Space','no_such_mesh','Sky']}"
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'only the usable level was taken'
    Assert-Equal -Expected 'Sky' -Actual $lod.Chain[1].Mesh -Message 'level 1 mesh'
    Set-Chain 'dome_a' "{'lods':['Sky']}"
}

Test 'a skinned mesh will not take an unskinned stand-in' {
    # The joint indices in a level's vertices index the skeleton LOD0 is animating,
    # since the level never reaches Components::Mesh::data. An unskinned stand-in
    # has none of that, and would draw in its bind pose while the model animates.
    SendOk 'place tf_troll origin' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    # Asked for rather than reproduced: the spawner owns instance naming
    # (World::InstanceEntityNames), and place leaves what it made selected.
    $troll = (SendOk 'list_selection')[0].Payload[0]
    Assert-True -Condition (-not [string]::IsNullOrEmpty($troll)) -Message 'the troll was placed'
    Set-Chain $troll "{'lods':['Sky']}"
    $lod = Get-Lod $troll
    Assert-Equal -Expected '0' -Actual $lod.levels -Message 'the chain was refused outright'
    SendOk "select $troll" | Out-Null
    SendOk 'delete' | Out-Null
}

#--- switching: screen area ---------------------------------------------------

Test 'up close the full mesh is drawn' {
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0}"
    Set-CameraDistance 8
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '0' -Actual $lod.current -Message 'current level'
    Assert-Equal -Expected $FullVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'far enough away the coarse level takes over' {
    # The measurement that makes this the screen-area rule and not a distance one:
    # level 1 has 1.25% of the vertices, so it is eligible once the model covers
    # about 1.25% of the viewport, which is what 600 units away does here.
    Set-CameraDistance 600
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '1' -Actual $lod.current -Message 'current level'
    # The selection has to reach the draw call and not just the readout: these are
    # the three arguments DrawIndexed is given.
    Assert-Equal -Expected $CoarseVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'coming back moves it back up' {
    Set-CameraDistance 8
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '0' -Actual $lod.current -Message 'current level'
    Assert-Equal -Expected $FullVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'the quality bias holds the full mesh further out' {
    # Same camera as the test that dropped to level 1, but asking for 200x the
    # detail - so the coverage that was enough for the coarse level no longer is.
    Set-CameraDistance 600
    Assert-Equal -Expected '1' -Actual (Get-Lod 'dome_a').current -Message 'level at bias 1'
    Set-Chain 'dome_a' "{'lod_bias':200.0}"
    Step-EditorFrames -Session $Session -Count 3
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').current -Message 'level at bias 200'
    Set-Chain 'dome_a' "{'lod_bias':1.0}"
}

#--- switching: camera distance -----------------------------------------------

Test 'in distance mode each level takes over at the distance set on it' {
    Set-Chain 'dome_a' "{'lod_mode':'distance','lods':[{'name':'Sky','distance':50}]}"
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected 'distance' -Actual $lod.mode -Message 'mode'
    Assert-Near -Expected 50.0 -Actual $lod.Chain[1].Distance -Message 'the authored switch point'

    Set-CameraDistance 8
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').current -Message 'inside the switch distance'
    Set-CameraDistance 120
    Assert-Equal -Expected '1' -Actual (Get-Lod 'dome_a').current -Message 'beyond it'
}

Test 'the distance rule ignores how big the model is on screen' {
    # The point of the mode: at 120 units the model covers far more of the screen
    # than the 1.25% the automatic rule would demand, and it is still on level 1.
    Set-CameraDistance 120
    Assert-Equal -Expected '1' -Actual (Get-Lod 'dome_a').current -Message 'coarse under the distance rule'
    Set-Chain 'dome_a' "{'lod_mode':'auto'}"
    Step-EditorFrames -Session $Session -Count 3
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').current -Message 'full detail under the area rule'
}

#--- per entity ---------------------------------------------------------------

Test 'lod_enabled pins one entity to full detail without touching the others' {
    # The chain is the asset's; whether an entity follows it is the entity's. Both
    # domes draw the same mesh and are the same distance out.
    Set-Chain 'dome_a' "{'lod_mode':'distance','lod_enabled':false}"
    Set-Chain 'dome_b' "{'lod_enabled':true}"
    Set-CameraDistance 120
    $a = Get-Lod 'dome_a'
    $b = Get-Lod 'dome_b'
    Assert-Equal -Expected '0' -Actual $a.enabled -Message 'dome_a opted out'
    Assert-Equal -Expected '0' -Actual $a.current -Message 'and is pinned to level 0'
    Assert-Equal -Expected $FullVertices -Actual ([int]$a.index_count) -Message 'drawing the full mesh'
    Assert-Equal -Expected '1' -Actual $b.current -Message 'dome_b still switches'
}

#--- serialization ------------------------------------------------------------

Test 'Mesh always serializes the chain, the mode and the bias' {
    # Unconditionally, like the smooth flag: every FromJson reads a missing key as
    # "leave alone", and undo works by replaying an earlier ToJson - so a key
    # omitted because it matched the default could never be restored.
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0,'lod_enabled':true,'lods':['Sky']}"
    $block = Get-Component -Session $Session -Entity 'dome_a' -Component 'Mesh'
    Assert-Equal -Expected 'auto' -Actual $block.lod_mode -Message 'lod_mode'
    Assert-Near -Expected 1.0 -Actual $block.lod_bias -Message 'lod_bias'
    Assert-Equal -Expected $true -Actual $block.lod_enabled -Message 'lod_enabled'
    Assert-Equal -Expected 1 -Actual @($block.lods).Count -Message 'one alternate'
    # Level 0 is never written back: it is the mesh, and reading it back in would
    # make the mesh an alternate of itself.
    Assert-Equal -Expected 'Sky' -Actual @($block.lods)[0].name -Message 'the alternate'
}

Test 'a bare array of names is accepted as well as the object form' {
    Set-Chain 'dome_a' "{'lods':['Sky']}"
    Assert-Equal -Expected '2' -Actual (Get-Lod 'dome_a').levels -Message 'string form'
    Set-Chain 'dome_a' "{'lods':[{'name':'Sky','distance':25.0}]}"
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'object form'
    Assert-Near -Expected 25.0 -Actual $lod.Chain[1].Distance -Message 'the distance came with it'
}

Test 'an added level is undoable' {
    Set-Chain 'dome_a' "{'lods':[]}"
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').levels -Message 'starting with no chain'
    Set-Chain 'dome_a' "{'lods':['Sky']}"
    Assert-Equal -Expected '2' -Actual (Get-Lod 'dome_a').levels -Message 'chain added'
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').levels -Message 'undone'
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected '2' -Actual (Get-Lod 'dome_a').levels -Message 'redone'
}

Test 'the chain survives a save and reload' {
    Set-Chain 'dome_a' "{'lod_mode':'distance','lod_bias':3.0,'lods':[{'name':'Sky','distance':70.0}]}"
    SendOk 'menu "File/Save Level"' | Out-Null
    # Saved into the entity's own record, which is what makes it a per-level edit
    # rather than a change to the mesh file.
    $saved = Get-Content -Raw (Join-Path $Assets 'Levels\Solo\1\level.json') | ConvertFrom-Json
    $record = $saved.world.instances | Where-Object { $_.name -eq 'dome_a' }
    Assert-Equal -Expected 'distance' -Actual $record.components.Mesh.lod_mode -Message 'mode in the file'
    Assert-Near -Expected 3.0 -Actual $record.components.Mesh.lod_bias -Message 'bias in the file'
    Assert-Equal -Expected 'Sky' -Actual @($record.components.Mesh.lods)[0].name -Message 'the alternate in the file'
    Assert-Near -Expected 70.0 -Actual @($record.components.Mesh.lods)[0].distance -Message 'its distance'
}
