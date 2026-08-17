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
# Puts the camera `Distance` from the origin, looking at it, and does not come back
# until the rig is actually there.
#
# The wait is the point. The pose is commanded on the channel's thread and applied by
# EditorCamera on its own tick, so the frames right after the command can still be
# rendering - and selecting levels of detail for - the previous distance. Asserting a
# fixed few frames later reads the old level roughly one run in fifteen, which is what
# made four of the switching tests below intermittent.
function Set-CameraDistance {
    param([double]$Distance)
    SendOk 'camera_target 0 0 0' | Out-Null
    SendOk "camera_pos 0 0 -$Distance" | Out-Null
    $tolerance = $Distance * 0.01 + 0.01
    for ($i = 0; $i -lt 30; $i += 2) {
        $cam = Get-Camera -Session $Session
        # world_position, not distance. `distance` is the rig's focus distance and
        # follows the command almost at once; `world_position` is the camera the
        # frame was actually drawn from, and SelectLods measures coverage from that
        # one. They used to be able to disagree forever - CameraSystem shared
        # Transform::dirty with StaticMeshSystem on a different timer, so a pose
        # could reach the Transform and never the view matrix (fixed in
        # Components::Camera, which now tracks its own inputs). Waiting on the
        # rendered pose is what makes this test say so if it ever comes back.
        $rendered = [Math]::Sqrt(($cam.world_position[0] * $cam.world_position[0]) +
                                 ($cam.world_position[1] * $cam.world_position[1]) +
                                 ($cam.world_position[2] * $cam.world_position[2]))
        if ([Math]::Abs([double]$cam.distance - $Distance) -le $tolerance -and
            [Math]::Abs($rendered - $Distance) -le $tolerance) {
            # There, and then a few frames for the render (and SelectLods with it) to
            # have run at the new pose.
            Step-EditorFrames -Session $Session -Count 3
            return
        }
        Step-EditorFrames -Session $Session -Count 2
    }
    $cam = Get-Camera -Session $Session
    throw ("camera never reached distance $Distance (focus $($cam.distance), " +
           "rendered at $($cam.world_position -join ', '))")
}

function Set-Chain {
    param([string]$Entity, [string]$Json)
    SendOk "set_component $Entity Mesh ""$Json""" | Out-Null
}

# Steps frames until `Entity` is drawn at level `Want`, up to a limit. The selection
# happens on the render thread from a screen coverage that settles over a few frames,
# with a 10% hysteresis margin on top, so a fixed frame count is a race. Throws if it
# never gets there, which is the regression this guards.
#
# Defined up here with the other helpers, not next to its first user: a suite file is
# executed top to bottom and a Test body runs as it is reached, so a function declared
# below one is simply not defined yet when that test runs.
function Wait-Lod {
    param([string]$Entity, [int]$Want, [int]$MaxFrames = 30)
    for ($i = 0; $i -lt $MaxFrames; $i += 2) {
        $lod = Get-Lod $Entity
        if ([int]$lod.current -eq $Want) { return $lod }
        Step-EditorFrames -Session $Session -Count 2
    }
    $l = Get-Lod $Entity
    throw ("$Entity never reached level $Want after $MaxFrames frames " +
           "(current=$($l.current) enabled=$($l.enabled) levels=$($l.levels) " +
           "mode=$($l.mode) bias=$($l.bias) index_count=$($l.index_count))")
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

# Every switching test below reads the level through Wait-Lod rather than Get-Lod,
# and that is not belt-and-braces. Set-CameraDistance returns when the *rig* reports
# the requested distance, but the level is chosen by SelectLods on the render thread
# from a screen coverage that settles over a few frames, with 10% of hysteresis on
# top - so "the camera is there" and "the frame has been drawn from there" are
# several frames apart, and a fixed count is a race that only shows up under load
# (it took a full-suite run to reproduce). Wait-Lod still throws if the level never
# arrives, with the readout in the message, so nothing is being papered over.

Test 'up close the full mesh is drawn' {
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0}"
    Set-CameraDistance 8
    $lod = Wait-Lod -Entity 'dome_a' -Want 0
    Assert-Equal -Expected $FullVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'far enough away the coarse level takes over' {
    # The measurement that makes this the screen-area rule and not a distance one:
    # level 1 has 1.25% of the vertices, so it is eligible once the model covers
    # about 1.25% of the viewport, which is what 600 units away does here.
    Set-CameraDistance 600
    $lod = Wait-Lod -Entity 'dome_a' -Want 1
    # The selection has to reach the draw call and not just the readout: these are
    # the three arguments DrawIndexed is given.
    Assert-Equal -Expected $CoarseVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'coming back moves it back up' {
    Set-CameraDistance 8
    $lod = Wait-Lod -Entity 'dome_a' -Want 0
    Assert-Equal -Expected $FullVertices -Actual ([int]$lod.index_count) -Message 'index count'
}

Test 'the quality bias holds the full mesh further out' {
    # Same camera as the test that dropped to level 1, but asking for 200x the
    # detail - so the coverage that was enough for the coarse level no longer is.
    Set-CameraDistance 600
    Wait-Lod -Entity 'dome_a' -Want 1 | Out-Null
    Set-Chain 'dome_a' "{'lod_bias':200.0}"
    Assert-Equal -Expected '0' -Actual (Wait-Lod -Entity 'dome_a' -Want 0).current -Message 'level at bias 200'
    Set-Chain 'dome_a' "{'lod_bias':1.0}"
}

#--- switching: camera distance -----------------------------------------------

Test 'in distance mode each level takes over at the distance set on it' {
    Set-Chain 'dome_a' "{'lod_mode':'distance','lods':[{'name':'Sky','distance':50}]}"
    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected 'distance' -Actual $lod.mode -Message 'mode'
    Assert-Near -Expected 50.0 -Actual $lod.Chain[1].Distance -Message 'the authored switch point'

    Set-CameraDistance 8
    Assert-Equal -Expected '0' -Actual (Wait-Lod -Entity 'dome_a' -Want 0).current -Message 'inside the switch distance'
    Set-CameraDistance 120
    Assert-Equal -Expected '1' -Actual (Wait-Lod -Entity 'dome_a' -Want 1).current -Message 'beyond it'
}

Test 'the distance rule ignores how big the model is on screen' {
    # The point of the mode: at 120 units the model covers far more of the screen
    # than the 1.25% the automatic rule would demand, and it is still on level 1.
    Set-CameraDistance 120
    Assert-Equal -Expected '1' -Actual (Wait-Lod -Entity 'dome_a' -Want 1).current -Message 'coarse under the distance rule'
    Set-Chain 'dome_a' "{'lod_mode':'auto'}"
    Assert-Equal -Expected '0' -Actual (Wait-Lod -Entity 'dome_a' -Want 0).current -Message 'full detail under the area rule'
}

#--- per entity ---------------------------------------------------------------

Test 'lod_enabled pins one entity to full detail without touching the others' {
    # The chain is the asset's; whether an entity follows it is the entity's. Both
    # domes draw the same mesh and are the same distance out.
    Set-Chain 'dome_a' "{'lod_mode':'distance','lod_enabled':false}"
    Set-Chain 'dome_b' "{'lod_enabled':true}"
    Set-CameraDistance 120
    # dome_b is the one that has to move; dome_a is asserted to have stayed put, so
    # it is read directly rather than waited for - waiting for a level something is
    # supposed to already be at would hide the opposite failure.
    $b = Wait-Lod -Entity 'dome_b' -Want 1
    $a = Get-Lod 'dome_a'
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

#--- generating a level -------------------------------------------------------
# A chain no longer needs a second model exported by hand: the editor builds the
# coarse mesh out of the fine one (Core::SimplifyMesh, through
# World::GenerateMeshLod) and registers it as a mesh asset of its own. These are
# the Components panel's "Generate level" button, and the cache file it leaves
# behind so the next load reads the geometry instead of computing it again.

# Generates a level for `Entity` and returns what came back on the OK line: the
# mesh it made and the two vertex counts. Asked for rather than reproduced - the
# name is chosen by the engine (`<source>_lod<n>`, first free n), and a test that
# guessed it would break the moment another test in this file generated one.
function New-GeneratedLod {
    param([string]$Entity, [double]$Percent)
    SendOk "select $Entity" | Out-Null
    $r = SendOk "generate_lod $Percent"
    $text = $r[0].Text
    $m = [regex]::Match($text, '^(\S+) vertices=(\d+) indices=(\d+) source=(\S+) source_vertices=(\d+)')
    if (-not $m.Success) { throw "generate_lod answered something unparseable: $text" }
    return @{
        Name           = $m.Groups[1].Value
        Vertices       = [int]$m.Groups[2].Value
        Indices        = [int]$m.Groups[3].Value
        Source         = $m.Groups[4].Value
        SourceVertices = [int]$m.Groups[5].Value
    }
}

Test 'generate_lod simplifies the mesh and installs the result as a level' {
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0,'lods':[]}"
    $made = New-GeneratedLod -Entity 'dome_a' -Percent 50
    Assert-Equal -Expected 'Space' -Actual $made.Source -Message 'built from the full mesh'
    Assert-Equal -Expected $FullVertices -Actual $made.SourceVertices -Message 'which is this one'
    # Near, not exact: a collapse takes a whole vertex and everything welded to it
    # at once, so the target is where to stop rather than a count to land on.
    Assert-Near -Expected ($FullVertices * 0.5) -Actual $made.Vertices `
        -Tolerance ($FullVertices * 0.05) -Message 'half the vertices'
    Assert-True -Condition ($made.Indices -lt $FullVertices) -Message 'and fewer triangles'

    $lod = Get-Lod 'dome_a'
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'the chain gained a level'
    Assert-Equal -Expected $made.Name -Actual $lod.Chain[1].Mesh -Message 'which is the generated mesh'
    # The ratio is derived from the geometry that was produced, so this is the
    # engine agreeing with what the reduction actually did.
    Assert-Near -Expected 0.5 -Actual $lod.Chain[1].Ratio -Tolerance 0.05 -Message 'its ratio'
    $script:GeneratedLod = $made
}

Test 'the generated level is a mesh asset like any other' {
    # The point of registering it rather than keeping it inside the chain: it can
    # be given to an entity, listed in a picker, and named by another level.
    $name = $script:GeneratedLod.Name
    Set-Chain 'dome_b' "{'name':'$name'}"
    $block = Get-Component -Session $Session -Entity 'dome_b' -Component 'Mesh'
    Assert-Equal -Expected $name -Actual $block.name -Message 'dome_b now draws the generated mesh'
    Set-Chain 'dome_b' "{'name':'Space'}"
}

Test 'a ratio that is not a reduction is refused' {
    SendOk 'select dome_a' | Out-Null
    Assert-Err -Result (Send 'generate_lod 150')[0] -Pattern 'between 0 and 1' `
        -Message 'more than the mesh has'
    Assert-Err -Result (Send 'generate_lod 0')[0] -Pattern 'between 0 and 1' `
        -Message 'nothing at all'
}

Test 'generating a level is undoable, and leaves the mesh behind' {
    Set-Chain 'dome_a' "{'lods':[]}"
    $made = New-GeneratedLod -Entity 'dome_a' -Percent 30
    Assert-Equal -Expected '2' -Actual (Get-Lod 'dome_a').levels -Message 'level added'
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected '0' -Actual (Get-Lod 'dome_a').levels -Message 'undone'
    # The asset is not undone with it - the same scope as an imported model, and
    # the reason redo can name it again.
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected $made.Name -Actual (Get-Lod 'dome_a').Chain[1].Mesh -Message 'redone'
}

Test 'a generated level is stored beside the level and recorded in it' {
    Set-Chain 'dome_a' "{'lod_mode':'auto','lods':[{'name':'$($script:GeneratedLod.Name)'}]}"
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content -Raw $LevelPath | ConvertFrom-Json
    $entry = @($level.world.generated_meshes) | Where-Object { $_.name -eq $script:GeneratedLod.Name }
    Assert-True -Condition ($null -ne $entry) -Message 'the level lists the generated mesh'
    Assert-Equal -Expected 'Space' -Actual $entry.source -Message 'and what it was made from'
    Assert-Near -Expected 0.5 -Actual $entry.ratio -Tolerance 0.001 -Message 'and at what ratio'
    # The recipe *and* the geometry: the second is a cache, which is why the first
    # is written at all.
    Assert-FileExists -Path (Join-Path $Assets $entry.file) -Message 'the cached geometry'
    $script:GeneratedFile = Join-Path $Assets $entry.file
}

Test 'a reloaded level gets its generated levels back' {
    # A second process on the file the test above wrote: the generated mesh has to
    # be registered before the templates are created, or the chain naming it would
    # be dropped as an unknown mesh.
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'lod-reload-channel'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Invoke-EditorCommand -Session $reloaded -Command @('select dome_a', 'lod_info')
        Assert-Ok -Result $r
        $line = @($r[1].Payload) | Where-Object { $_ -match '^\s+lod1 ' }
        Assert-Match -Pattern "mesh=$([regex]::Escape($script:GeneratedLod.Name))" -Actual $line `
            -Message 'the generated level came back'
        Assert-Match -Pattern "vertices=$($script:GeneratedLod.Vertices)\b" -Actual $line `
            -Message 'with the same geometry, off the cache file'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'a damaged cache is rebuilt rather than trusted' {
    # The file describes the mesh it was made from, so anything that no longer
    # matches - a changed model, another build, a truncated write - is thrown away
    # and simplified again. Nothing has to be invalidated by hand, and a stale file
    # can never draw yesterday's model as today's level.
    [IO.File]::WriteAllText($script:GeneratedFile, 'not a mesh')
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'lod-rebuild-channel'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Invoke-EditorCommand -Session $reloaded -Command @('select dome_a', 'lod_info')
        Assert-Ok -Result $r
        $line = @($r[1].Payload) | Where-Object { $_ -match '^\s+lod1 ' }
        Assert-Match -Pattern "vertices=$($script:GeneratedLod.Vertices)\b" -Actual $line `
            -Message 'rebuilt to the same geometry'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
    # And rewritten, so the load after this one is a read again.
    Assert-True -Condition ((Get-Item $script:GeneratedFile).Length -gt 1000) `
        -Message 'the cache file was rewritten'
}

Test 'a skinned mesh generates a level skinned to the same skeleton' {
    # The one case a stand-in cannot be picked for: the vertices carry joint
    # indices into the skeleton LOD0 is animating, so a hand-made LOD has to be
    # rigged to it. A generated one is made of those same vertices, so it always
    # is - and this is the check that the reduction carries the weights across.
    SendOk 'place tf_troll origin' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $troll = (SendOk 'list_selection')[0].Payload[0]
    $made = New-GeneratedLod -Entity $troll -Percent 25
    $lod = Get-Lod $troll
    Assert-Equal -Expected '2' -Actual $lod.levels -Message 'the chain was accepted'
    Assert-Equal -Expected $made.Name -Actual $lod.Chain[1].Mesh -Message 'the generated level'
    Assert-Near -Expected 0.25 -Actual $lod.Chain[1].Ratio -Tolerance 0.05 -Message 'its ratio'
    SendOk "select $troll" | Out-Null
    SendOk 'delete' | Out-Null
}

#--- what the ray tracers trace ------------------------------------------------
# The chain is not only about what is drawn. Every ray - reflection, refraction and
# indirect light alike - traces the *coarsest* level the mesh has, whatever is on
# screen and whatever the ray tracing quality is set to. `rt_info` reports the
# geometry the last PrepareRT handed them, summed in triangle indices over the
# objects it sent, and it is the only place any of this is observable: a ray hitting
# the wrong geometry produces a reflection that still looks like a reflection.

# rt_info, after enough frames for the ray tracing thread to have re-prepared the
# scene with whatever was just changed.
function Get-RtInfo {
    Step-EditorFrames -Session $Session -Count 4
    $r = SendOk 'rt_info'
    $info = @{}
    foreach ($field in @('objects', 'full_indices', 'traced_indices')) {
        $m = [regex]::Match($r[0].Text, "$field=([0-9]+)")
        if (-not $m.Success) { throw "rt_info has no $field`: $($r[0].Text)" }
        $info[$field] = [int64]$m.Groups[1].Value
    }
    return $info
}

Test 'every ray traces the coarsest level, whatever is being drawn' {
    # dome_a up close is drawn at full detail; the tracers still get the coarse
    # geometry. Then the coarse level is made the drawn one too (distance mode with
    # a switch point of 0, so it is eligible everywhere - no camera involved, see the
    # note in the switching tests) and what is traced does not move.
    SendOk 'render rt_quality high' | Out-Null
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0,'lods':['Sky']}"
    Set-CameraDistance 8
    Wait-Lod -Entity 'dome_a' -Want 0 | Out-Null
    $drawn_fine = Get-RtInfo
    Assert-True -Condition ($drawn_fine.objects -gt 0) -Message 'the ray tracers were given objects'
    Assert-True -Condition ($drawn_fine.traced_indices -lt $drawn_fine.full_indices) `
        -Message ("traced less than the full scene " +
                  "($($drawn_fine.traced_indices) of $($drawn_fine.full_indices))")

    Set-Chain 'dome_a' "{'lod_mode':'distance','lods':[{'name':'Sky','distance':0.0}]}"
    Wait-Lod -Entity 'dome_a' -Want 1 | Out-Null
    $drawn_coarse = Get-RtInfo
    Assert-Equal -Expected $drawn_fine.traced_indices -Actual $drawn_coarse.traced_indices `
        -Message 'the drawn level does not change what is traced'
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0}"
}

Test 'the ray tracing quality changes resolution, not traced geometry' {
    # It used to be a floor on the traced level (high = what is drawn, mid = level 1,
    # low = level 2). It is not any more: every quality traces the coarsest level and
    # the setting moves RT_TEXTURE_RESOLUTION_DIVIDER instead, which is where its cost
    # actually is.
    Set-Chain 'dome_a' "{'lod_mode':'auto','lod_bias':1.0,'lods':['Sky']}"
    Set-CameraDistance 8
    Wait-Lod -Entity 'dome_a' -Want 0 | Out-Null
    SendOk 'render rt_quality high' | Out-Null
    $high = Get-RtInfo
    SendOk 'render rt_quality mid' | Out-Null
    $mid = Get-RtInfo
    SendOk 'render rt_quality low' | Out-Null
    $low = Get-RtInfo
    Assert-Equal -Expected $high.traced_indices -Actual $mid.traced_indices -Message 'mid traces the same'
    Assert-Equal -Expected $high.traced_indices -Actual $low.traced_indices -Message 'low traces the same'
    Assert-True -Condition ($high.traced_indices -lt $high.full_indices) `
        -Message 'and it is the coarse geometry, not the full mesh'
    SendOk 'render rt_quality high' | Out-Null
}

Test 'a mesh with no chain is traced at full detail' {
    # The floor of the rule: "coarsest level there is" is the mesh itself when nobody
    # has built levels for it, so a scene with no chains traces everything.
    Set-Chain 'dome_a' "{'lods':[]}"
    Set-Chain 'dome_b' "{'lods':[]}"
    $info = Get-RtInfo
    Assert-Equal -Expected $info.full_indices -Actual $info.traced_indices `
        -Message 'nothing to coarsen, so nothing is coarsened'
}
