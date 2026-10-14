# fixture: empty
# description: Reflections solved on the screen, in the world, or both - the same scene three ways, the counters that say where each pixel was answered, and that what the screen answered is not traced again.

# A 60 x 60 mirror floor with two tall boxes on it, seen from above and to one side. The floor
# reflects the boxes: both are on screen, so the screen march should find them, and the world
# tracer (the BVH of the nearest hundred objects) finds the same ones.
#
# What each solver did cannot be told from the picture - they are meant to agree - so the tests
# read `rt_stats`: of the pixels that reflect, how many the screen answered and how many were
# listed for the world tracer. The counters are read back a few frames late, and asking is what
# turns them on, hence the polling.
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on
# the viewport).

function Get-RtStats {
    $r = (SendOk 'rt_stats')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
    return [pscustomobject]$o
}

function Wait-RtStats {
    param([scriptblock]$Until, [string]$What)
    for ($i = 0; $i -lt 40; $i++) {
        Step-EditorFrames -Session $Session -Count 5
        $s = Get-RtStats
        if (& $Until $s) { return $s }
    }
    throw "rt_stats never showed $What (last: $((SendOk 'rt_stats')[0].Text))"
}

function Use-Space {
    param([string]$Space, [int]$Steps = 0)
    SendOk "render rt_space $Space", "render rt_screen_steps $Steps" | Out-Null
    Step-EditorFrames -Session $Session -Count 20
}

# The reflection buffer under the tall box: its mirror image, seen from here.
function Get-FootprintShot {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 30
    $path = Shot $Name
    return Get-ImageStats -Path $path -Left 0.575 -Top 0.56 -Right 0.645 -Bottom 0.70 -Step 2
}

Test 'set up: a mirror floor and two boxes on it, lit by the sun' {
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 60 0.2 60' | Out-Null
    SendOk 'select box_b',
        "set_component box_b Physics ""{'type':'STATIC'}""",
        'set_position -4 4 6', 'set_scale 4 8 4' | Out-Null
    SendOk 'select box_c',
        "set_component box_c Physics ""{'type':'STATIC'}""",
        'set_position 6 3 10', 'set_scale 4 6 4' | Out-Null
    # The floor is a perfect mirror (dispersion 1 - specular = 0), the boxes are not: every ray off
    # the floor is the mirror direction, so what it should find is a matter of geometry.
    SendOk 'set_material box_a TestBlue', 'set_material_prop TestBlue specular 1.0',
        'set_material_prop TestBlue rt_reflex 1' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 14 -22', 'camera_target 0 2 6' | Out-Null
    SendOk 'render rt_quality high', 'render rt_reflections 1', 'render rt_refractions 1', 'render rt_indirect 0',
        'render aa 0', 'render motion_blur 0' | Out-Null
}

Test 'rt_space, rt_screen_steps and shadow_space are render settings' {
    foreach ($space in @('screen', 'world', 'both')) {
        $r = (SendOk "render rt_space $space")[0].Text | ConvertFrom-Json
        Assert-Equal -Expected $space -Actual $r.rt_space
        $r = (SendOk "render shadow_space $space")[0].Text | ConvertFrom-Json
        Assert-Equal -Expected $space -Actual $r.shadow_space
    }
    Assert-Err -Result (Send 'render rt_space somewhere')[0] -Pattern 'screen\|world\|both'
    Assert-Err -Result (Send 'render shadow_space somewhere')[0] -Pattern 'screen\|world\|both'
    $r = (SendOk 'render rt_screen_steps 64')[0].Text | ConvertFrom-Json
    Assert-Equal -Expected 64 -Actual $r.rt_screen_steps
    Assert-Err -Result (Send 'render rt_screen_steps -1')[0] -Pattern 'rt_screen_steps'
    SendOk 'render rt_screen_steps 0', 'render rt_space world', 'render shadow_space world' | Out-Null
}

Test 'world only: nothing is solved on the screen and nothing is listed' {
    Use-Space 'world'
    $s = Get-RtStats
    # The world tracer alone walks the screen, a thread a pixel, as it always did: there is no
    # screen pass to count anything.
    Assert-Equal -Expected 0 -Actual $s.screen_solved -Message 'the screen answered nothing'
    Assert-Equal -Expected 0 -Actual $s.world_reflect -Message 'and there is no list'
}

Test 'screen only: the screen answers what it can and the rest is left black' {
    Use-Space 'screen'
    $s = Wait-RtStats -Until { param($x) $x.screen_solved -gt 0 } -What 'solved pixels'
    Assert-True -Condition ($s.reflective -gt 1000) -Message "the floor reflects ($($s.reflective) pixels)"
    Assert-True -Condition ($s.screen_solved -gt 1000) -Message "the boxes are on screen, so some rays find them ($($s.screen_solved))"
    Assert-True -Condition ($s.screen_solved -lt $s.reflective) -Message 'and not every pixel reflects something drawn'
    Assert-Equal -Expected 0 -Actual $s.world_reflect -Message 'nothing is left for a world tracer that is off'
}

Test 'both: what the screen solved is not traced again, and everything else is' {
    Use-Space 'both'
    $s = Wait-RtStats -Until { param($x) $x.screen_solved -gt 0 -and $x.world_reflect -gt 0 } -What 'solved and listed pixels'
    # One list entry per reflective pixel the screen did not answer, and no more: the world
    # tracer runs over exactly the rays that are left. This is the whole point of the lists.
    Assert-Equal -Expected $s.reflective -Actual ($s.screen_solved + $s.world_reflect) `
        -Message "screen $($s.screen_solved) + listed $($s.world_reflect) = reflective $($s.reflective)"
}

Test 'the screen finds the reflection of the boxes the world finds' {
    SendOk 'render debug_buffer reflection', 'render debug_gain 4' | Out-Null
    $means = @{}
    foreach ($space in @('world', 'screen', 'both')) {
        Use-Space $space
        $means[$space] = (Get-FootprintShot "mirror-$space").Mean
    }
    SendOk 'render debug_buffer off' | Out-Null
    # Under the base of the tall box the floor shows its mirror image: far above the black the
    # reflection buffer holds where nothing is reflected, whichever solver drew it.
    foreach ($space in @('world', 'screen', 'both')) {
        Assert-True -Condition ($means[$space] -gt 30.0) `
            -Message ("the mirror image is there with $space ({0:N1}, black is 0)" -f $means[$space])
    }
}

Test 'fewer steps solve fewer rays on the screen' {
    Use-Space 'screen' -Steps 6
    $few = Wait-RtStats -Until { param($x) $x.screen_solved -gt 0 } -What 'solved pixels at 6 steps'
    Use-Space 'screen' -Steps 96
    $many = Wait-RtStats -Until { param($x) $x.screen_solved -gt $few.screen_solved } -What 'more solved pixels at 96 steps'
    Assert-True -Condition ($many.screen_solved -gt $few.screen_solved * 1.2) `
        -Message "96 steps solved $($many.screen_solved), 6 steps $($few.screen_solved)"
    SendOk 'render rt_screen_steps 0' | Out-Null
}

Test 'with reflections off nothing is solved and nothing is listed' {
    Use-Space 'both'
    SendOk 'render rt_reflections 0' | Out-Null
    $s = Wait-RtStats -Until { param($x) $x.screen_solved -eq 0 -and $x.world_reflect -eq 0 } -What 'a quiet pass'
    Assert-Equal -Expected 0 -Actual $s.screen_solved
    SendOk 'render rt_reflections 1' | Out-Null
    $s = Wait-RtStats -Until { param($x) $x.screen_solved -gt 0 } -What 'solved pixels again'
    Assert-True -Condition ($s.screen_solved -gt 0) -Message 'and back'
}

Test 'back to the world solver the level was authored with' {
    SendOk 'render rt_space world', 'render rt_screen_steps 0' | Out-Null
    $r = (SendOk 'render')[0].Text | ConvertFrom-Json
    Assert-Equal -Expected 'world' -Actual $r.rt_space
}
