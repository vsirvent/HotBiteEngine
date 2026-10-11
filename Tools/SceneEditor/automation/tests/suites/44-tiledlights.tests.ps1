# fixture: empty
# description: Tiled deferred lighting - every point light is packed into one 1024 entry buffer, a compute pass records which of them reach each 16x16 screen tile, and the lighting is added to the G-buffer afterwards. 100 lights are cheap, 1024 lights over a floor work and each tile still sees only a few of them, the culling loses no light and costs a fraction of shading them all.

# What is measured, and how, because "performance" and "correct" are both easy to get wrong here:
#
# * COUNTS come off the GPU: `lights_info` is what the culling pass wrote (how many tiles hold a
#   light, the most lights in one tile, the average over the tiles that hold any). They are the
#   evidence for "only a few of the 1024 are in a tile's mask" - not a timing, so not noisy.
# * NOTHING IS LOST: `lights_validate` tests every pixel against every light by brute force and
#   counts the lights that reach a pixel and are missing from its tile's mask. 0, always.
# * SAME PICTURE: `tile_culling off` puts every light in every tile - the answer shading every
#   light everywhere would give. A light out of range adds nothing, so it must be the same image,
#   and what it costs is what the culling saves.
# * TIMES come from the GPU's own timestamp queries (`gpu_timings`), summed over frames and
#   differenced between two reads; asking is what turns the queries on and they land a few
#   frames late. The assertions are ratios and ceilings with room, not a number tied to a card.
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on
# the viewport); the camera looks down on a 60 x 60 floor.

$invariant = [Globalization.CultureInfo]::InvariantCulture
function F { param([double]$Value) return $Value.ToString($invariant) }

function Get-LightsInfo {
    $r = (SendOk 'lights_info')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]@{
        Lights = [int]$o['lights']; Shadowed = [int]$o['shadowed']; Lit = [int]$o['lit']; Geometry = [int]$o['geometry']
        Max = [int]$o['max']; Avg = [double]::Parse($o['avg'], $invariant); Valid = ($o['valid'] -eq '1'); Tiles = $o['tiles']
    }
}

# lights_info is read back a few frames late: the light count is the frame's own, but the tile
# counters come out of an 8 deep readback ring. Step until the count is the one asked for, then
# past the ring, so the counters describe the scene as it is now.
function Wait-LightsInfo {
    param([int]$Lights)
    for ($i = 0; $i -lt 30; $i++) {
        Step-EditorFrames -Session $Session -Count 5
        $info = Get-LightsInfo
        if ($info.Valid -and $info.Lights -eq $Lights) {
            Step-EditorFrames -Session $Session -Count 12
            return Get-LightsInfo
        }
    }
    throw "lights_info never reported $Lights lights (last: $((SendOk 'lights_info')[0].Text))"
}

function Get-GpuTimings {
    $r = (SendOk 'gpu_timings')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([-0-9.eE+]+)')) { $o[$m.Groups[1].Value] = [double]::Parse($m.Groups[2].Value, $invariant) }
    return $o
}

# Milliseconds per frame of each pass over `Frames` frames, from two reads of the cumulative sums.
function Measure-Gpu {
    param([int]$Frames = 60)
    Get-GpuTimings | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    $a = Get-GpuTimings
    Step-EditorFrames -Session $Session -Count $Frames
    Step-EditorFrames -Session $Session -Count 10
    $b = Get-GpuTimings
    $n = $b['frames'] - $a['frames']
    if ($n -lt ($Frames / 2)) { throw "only $n frames were timed" }
    $o = @{ frames = $n }
    foreach ($k in 'shadows', 'depth', 'scene', 'tile_cull', 'deferred') { $o[$k] = ($b[$k] - $a[$k]) / $n }
    return [pscustomobject]$o
}

function Get-SceneStats {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 12
    $path = Shot $Name
    return [pscustomobject]@{ Path = $path; Stats = (Get-ImageStats -Path $path -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3) }
}

Test 'set up: a floor, no sun, and the camera above it' {
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 60 0.2 60' | Out-Null
    # The sun and the ambient light would flood the floor and hide the point lights.
    SendOk "set_component sun DirectionalLight ""{'intensity':0}""",
        "set_component ambient AmbientLight ""{'color_down':{'x':0,'y':0,'z':0},'color_up':{'x':0,'y':0,'z':0}}""" | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk 'select box_a', 'focus' | Out-Null
    SendOk 'camera_pos 0 38 -34' | Out-Null
    SendOk 'light_gizmos off', 'light_positions off' | Out-Null
    Step-EditorFrames -Session $Session -Count 8
    $info = Wait-LightsInfo -Lights 0
    Assert-Equal -Expected 0 -Actual $info.Lights -Message 'the fixture has no point light'
    Assert-Equal -Expected 0 -Actual $info.Lit -Message 'and no tile holds one'
}

Test 'a light is packed into the buffer and decodes to what was authored' {
    SendOk 'spawn_lights 1 14 3 0 5' | Out-Null
    $info = Wait-LightsInfo -Lights 1
    Assert-Equal -Expected 1 -Actual $info.Lights -Message 'one light packed'
    $r = (SendOk 'lights_packed 0')[0].Text
    $m = [regex]::Match($r, 'pos=([-\d.]+),([-\d.]+),([-\d.]+) range=([-\d.]+) color=([-\d.]+),([-\d.]+),([-\d.]+) spot=(\d) .* slot=(-?\d+)')
    Assert-True -Condition $m.Success -Message "unparseable lights_packed: $r"
    Assert-Near -Expected 5.0 -Actual ([double]::Parse($m.Groups[2].Value, $invariant)) -Tolerance 0.001 -Message 'height is exact (a float)'
    Assert-Near -Expected 14.0 -Actual ([double]::Parse($m.Groups[4].Value, $invariant)) -Tolerance 0.001 -Message 'range is exact (a float)'
    $comp = Get-Component -Session $Session -Entity 'tl_0' -Component 'PointLight'
    $packed = @(5, 6, 7 | ForEach-Object { [double]::Parse($m.Groups[$_].Value, $invariant) })
    $authored = @([double]$comp.color.x, [double]$comp.color.y, [double]$comp.color.z)
    for ($i = 0; $i -lt 3; $i++) {
        # RGB9E5: 9 bits of mantissa under a shared exponent - the largest channel is within 0.4%,
        # the smaller ones within a fraction of the largest's step.
        Assert-Near -Expected $authored[$i] -Actual $packed[$i] -Tolerance ([Math]::Max($authored[$i], 0.05) * 0.01 + 0.012) `
            -Message "colour channel $i survives the packing (authored $($authored[$i]), packed $($packed[$i]))"
    }
    Assert-Equal -Expected '0' -Actual $m.Groups[8].Value -Message 'a point light is not a spot'
    Assert-Equal -Expected '-1' -Actual $m.Groups[9].Value -Message 'and holds no shadow slot'
    SendOk 'clear_lights' | Out-Null
}

Test 'a point light lights the floor in its range and leaves the rest dark' {
    SendOk 'clear_lights' | Out-Null
    Wait-LightsInfo -Lights 0 | Out-Null
    $dark = Get-SceneStats 'no-light'
    SendOk 'spawn_lights 1 9 4 0 3' | Out-Null
    $info = Wait-LightsInfo -Lights 1
    $lit = Get-SceneStats 'one-light'
    Assert-True -Condition ($info.Lit -gt 0) -Message "tiles hold the light ($($info.Lit) of $($info.Geometry) tiles with a surface)"
    Assert-True -Condition ($info.Lit -lt $info.Geometry / 2) -Message "but not most tiles: the light reaches a pool of the floor ($($info.Lit) of $($info.Geometry))"
    Assert-True -Condition ($dark.Stats.LitShare -lt 0.02) -Message ("with no light the floor is dark ({0:P1} lit)" -f $dark.Stats.LitShare)
    Assert-True -Condition ($lit.Stats.LitShare -gt $dark.Stats.LitShare + 0.01) `
        -Message ("a light puts a pool of light on the floor ({0:P1} lit, was {1:P1})" -f $lit.Stats.LitShare, $dark.Stats.LitShare)
    Assert-True -Condition ($lit.Stats.LitShare -lt 0.6) -Message ("and it does not light all of the floor ({0:P1})" -f $lit.Stats.LitShare)
    SendOk 'clear_lights' | Out-Null
}

Test '100 point lights: packed, culled to a few per tile, and cheap' {
    SendOk 'clear_lights' | Out-Null
    SendOk 'spawn_lights 100 10 3 56 4' | Out-Null
    $info = Wait-LightsInfo -Lights 100
    Assert-Equal -Expected 100 -Actual $info.Lights -Message '100 lights are packed'
    Assert-True -Condition ($info.Lit -gt $info.Geometry / 3) -Message "most of the floor is lit ($($info.Lit) of $($info.Geometry) tiles)"
    # A point of the floor is reached by about 8 of them (a 10 unit range over a 56 unit floor), so
    # a tile holding 100 would mean the culling does nothing.
    Assert-True -Condition ($info.Max -lt 45) -Message "the busiest tile holds $($info.Max) of the 100 lights"
    Assert-True -Condition ($info.Avg -lt 22) -Message "a lit tile holds $($info.Avg) lights on average"
    $t = Measure-Gpu -Frames 60
    Write-Host ("    100 lights: tile_cull {0:N3} ms, deferred {1:N3} ms, scene {2:N3} ms per frame" -f $t.tile_cull, $t.deferred, $t.scene)
    Assert-True -Condition ($t.tile_cull -gt 0 -and $t.deferred -gt 0) -Message 'both lighting passes were timed'
    Assert-True -Condition (($t.tile_cull + $t.deferred) -lt 4.0) -Message ("culling and shading 100 lights take {0:N3} ms a frame" -f ($t.tile_cull + $t.deferred))
    $script:cost100 = $t.tile_cull + $t.deferred
}

Test '100 point lights: the masks lose no light' {
    $v = (SendOk 'lights_validate')[0].Text
    $m = [regex]::Match($v, 'pixels=(\d+) reached=(\d+) missed=(\d+)')
    Assert-True -Condition $m.Success -Message "unparseable lights_validate: $v"
    Assert-True -Condition ([long]$m.Groups[1].Value -gt 100000) -Message "$($m.Groups[1].Value) pixels checked"
    Assert-True -Condition ([long]$m.Groups[2].Value -gt 100000) -Message "$($m.Groups[2].Value) (pixel, light) pairs reach each other"
    Assert-Equal -Expected 0 -Actual ([long]$m.Groups[3].Value) -Message 'no pixel misses a light that reaches it'
}

Test '1024 point lights over the floor: all packed, a tile still sees only a few' {
    SendOk 'clear_lights' | Out-Null
    # A range of 3 at a height of 2 reaches 2.2 units of floor: a point of it is under about 5 lamps,
    # so the answer to "how many of 1024 does a tile need" is a handful, not a tenth.
    SendOk 'spawn_lights 1024 3 3 58 2' | Out-Null
    $info = Wait-LightsInfo -Lights 1024
    Assert-Equal -Expected 1024 -Actual $info.Lights -Message 'the buffer holds 1024 lights'
    Assert-True -Condition ($info.Lit -gt $info.Geometry / 2) -Message "most of the floor is lit ($($info.Lit) of $($info.Geometry) tiles)"
    # The whole point: 1024 lights in the scene, a handful in each tile's mask.
    Assert-True -Condition ($info.Max -lt 64) -Message "the busiest tile holds $($info.Max) of the 1024 lights"
    Assert-True -Condition ($info.Avg -lt 24) -Message "a lit tile holds $($info.Avg) lights on average (of 1024)"
    Assert-True -Condition ($info.Avg -lt 1024 / 40) -Message "that is under a fortieth of the lights"
    Write-Host ("    1024 lights: a lit tile holds {0:N1} of them on average, the busiest {1}" -f $info.Avg, $info.Max)
    $v = (SendOk 'lights_validate')[0].Text
    $m = [regex]::Match($v, 'reached=(\d+) missed=(\d+)')
    Assert-True -Condition ([long]$m.Groups[1].Value -gt 1000000) -Message "$($m.Groups[1].Value) (pixel, light) pairs reach each other"
    Assert-Equal -Expected 0 -Actual ([long]$m.Groups[2].Value) -Message '1024 lights: the masks lose none'
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

Test '1024 point lights: the frame is not 10 times the cost of 100' {
    $t = Measure-Gpu -Frames 60
    Write-Host ("    1024 lights: tile_cull {0:N3} ms, deferred {1:N3} ms, scene {2:N3} ms per frame" -f $t.tile_cull, $t.deferred, $t.scene)
    $cost = $t.tile_cull + $t.deferred
    Assert-True -Condition ($cost -lt 6.0) -Message ("culling and shading 1024 lights take {0:N3} ms a frame" -f $cost)
    # Ten times the lights, but a tile still looks at a few: well under ten times the work.
    Assert-True -Condition ($cost -lt [Math]::Max($script:cost100 * 6.0, 1.0)) `
        -Message ("1024 lights cost {0:N3} ms, 100 lights cost {1:N3} ms" -f $cost, $script:cost100)
}

Test '1024 point lights: culling on or off, the same picture; off costs far more' {
    SendOk 'tile_culling on' | Out-Null
    $on = Get-SceneStats 'culled'
    $tOn = Measure-Gpu -Frames 40
    SendOk 'tile_culling off' | Out-Null
    $off = Get-SceneStats 'brute-force'
    $tOff = Measure-Gpu -Frames 40
    SendOk 'tile_culling on' | Out-Null
    Write-Host ("    deferred: culled {0:N3} ms, every light everywhere {1:N3} ms; cull pass {2:N3} ms" -f $tOn.deferred, $tOff.deferred, $tOn.tile_cull)
    # A light out of range adds nothing, so the two frames are the same image up to the noise of
    # the frame (temporal GI) - which is what the picture of two culled frames differs by too.
    $on2 = Get-SceneStats 'culled-again'
    $noise = Get-ImageDifference -PathA $on.Path -PathB $on2.Path -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3 -Threshold 6
    $diff = Get-ImageDifference -PathA $on.Path -PathB $off.Path -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3 -Threshold 6
    Assert-True -Condition ($diff.MeanDelta -le [Math]::Max($noise.MeanDelta * 3.0, 1.0)) `
        -Message ("culled vs brute force: mean delta {0:N2} (two culled frames: {1:N2})" -f $diff.MeanDelta, $noise.MeanDelta)
    Assert-True -Condition ($diff.DifferingShare -le [Math]::Max($noise.DifferingShare * 3.0, 0.01)) `
        -Message ("culled vs brute force: {0:P2} of the pixels differ by more than 6 (two culled frames: {1:P2})" -f $diff.DifferingShare, $noise.DifferingShare)
    Assert-True -Condition ($tOff.deferred -gt $tOn.deferred * 3.0) `
        -Message ("shading every light everywhere takes {0:N3} ms, with the culling {1:N3} ms" -f $tOff.deferred, $tOn.deferred)
}

Test 'more lights than the buffer holds: the nearest 1024 are lit, nothing crashes' {
    SendOk 'spawn_lights 200 3 3 58 2' | Out-Null
    $info = Wait-LightsInfo -Lights 1024
    Assert-Equal -Expected 1024 -Actual $info.Lights -Message '1224 lights, 1024 packed'
    Step-EditorFrames -Session $Session -Count 10
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

Test 'shadow casters beyond the shadow slots still light, and only MAX_LIGHTS hold a map' {
    SendOk 'clear_lights' | Out-Null
    SendOk 'spawn_lights 12 14 3 40 6 1' | Out-Null
    $info = Wait-LightsInfo -Lights 12
    Assert-Equal -Expected 8 -Actual $info.Shadowed -Message '12 shadow casters, 8 shadow slots'
    $shot = Get-SceneStats 'shadowed'
    Assert-True -Condition ($shot.Stats.Mean -gt 5) -Message "the floor is lit ($($shot.Stats.Mean))"
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

Test 'a spotlight keeps its cone through the packing' {
    SendOk 'clear_lights' | Out-Null
    SendOk 'spawn_lights 1 40 4 0 10' | Out-Null
    $flat = Get-SceneStats 'omni'
    SendOk "set_component tl_0 PointLight ""{'spot':true,'spot_inner':8,'spot_outer':14}""", 'select tl_0', 'set_rotation 90 0 0' | Out-Null
    $cone = Get-SceneStats 'cone'
    $r = (SendOk 'lights_packed 0')[0].Text
    Assert-Match -Pattern 'spot=1' -Actual $r -Message "the light is packed as a spot ($r)"
    Assert-True -Condition ($cone.Stats.LitShare -lt $flat.Stats.LitShare) `
        -Message ("the cone lights less of the floor than the omni light (cone {0:P1}, omni {1:P1})" -f $cone.Stats.LitShare, $flat.Stats.LitShare)
}

Test 'removing every light puts the frame back to no lights, with no pass left running' {
    SendOk 'clear_lights' | Out-Null
    $info = Wait-LightsInfo -Lights 0
    Assert-Equal -Expected 0 -Actual $info.Lit -Message 'no tile holds a light'
    $t = Measure-Gpu -Frames 30
    Assert-True -Condition ($t.tile_cull -lt 0.05) -Message ("the culling dispatches nothing with no lights ({0:N3} ms)" -f $t.tile_cull)
}
