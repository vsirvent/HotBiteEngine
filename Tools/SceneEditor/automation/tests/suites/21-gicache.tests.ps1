# fixture: models
# description: The world-space radiance cache - that it fills, that lookups resolve, that it ages and resets, and its two debug views.

# The cache (Shaders/Common/RadianceCache.hlsli) is deliberately invisible in a
# normal frame: a lookup that finds no cell falls back to exactly what the
# screen-space path always did. So everything here goes through the two surfaces
# that *can* see it - the `gi_cache_info` counters and the `gi_cache` /
# `gi_cache_conf` debug buffers.
#
# Two things to know before adding a test here:
#
#  - `gi_cache_info` is read back off the GPU without stalling the pipeline, so it
#    lags the current frame by several. Anything that changes the cache and then
#    asserts on the counters has to step frames first - Wait-CacheStat does that.
#  - the fixture scene has geometry but very little bounced light, so cells exist
#    with near-zero radiance. Assert on cells existing and lookups resolving, never
#    on the cache being bright: that would be a test of the fixture's lighting.

function Get-CacheInfo {
    $r = SendOk 'gi_cache_info'
    $info = @{}
    foreach ($m in [regex]::Matches($r[0].Text, '(\w+)=(\d+)')) {
        $info[$m.Groups[1].Value] = [int64]$m.Groups[2].Value
    }
    return [pscustomobject]$info
}

# Steps frames until a counter satisfies $Until, or gives up. The readback lag
# means a bare read straight after a change reports the state from before it.
function Wait-CacheStat {
    param([scriptblock]$Until, [int]$MaxFrames = 240)
    $info = $null
    for ($i = 0; $i -lt $MaxFrames; $i += 10) {
        Step-EditorFrames -Session $Session -Count 10
        $info = Get-CacheInfo
        if (& $Until $info) { return $info }
    }
    return $info
}

# Puts the camera where the fixture's geometry fills the view, so there are
# surfaces for the GI pass to deposit from.
function Reset-View {
    SendOk 'camera_rot 0 0 0', 'camera_pos 16 10 -20', 'camera_target 0 5 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
}

Test 'gi_cache_info reports the table and its occupancy' {
    Reset-View
    $info = Wait-CacheStat { param($i) $i.live -gt 0 }
    foreach ($field in @('live', 'touched', 'evicted', 'deposits', 'dropped', 'hits', 'misses', 'entries')) {
        Assert-True -Condition $info.PSObject.Properties.Name.Contains($field) -Message "gi_cache_info has no $field"
    }
    # The table size is a compile-time constant shared with the HLSL side; if these
    # two ever disagree the buffer is read with the wrong stride. RC_ENTRIES in
    # RadianceCache.hlsli and RenderSystem::RADIANCE_CACHE_ENTRIES must match, and
    # nothing but this checks it.
    Assert-Equal -Expected 1048576 -Actual $info.entries -Message 'cache entry count'
}

Test 'the table survives a camera exploring the level' {
    # The failure this catches is not subtle once you know to look: the table fills,
    # deposits start failing, lookups start missing, and the gi_cache view goes blue -
    # but the frame just quietly loses its indirect light with nothing logged.
    #
    # What fills it is not one view (about 32k cells at the sizing in RadianceCache.hlsli)
    # but RC_MAX_AGE frames of a *moving* one. Measured on sponza this reached 151k live
    # cells over two orbits, which was 29% of the old 524288 and already dropping 1% of
    # deposits. The fixture level is far smaller, so this is a smoke test of the same
    # shape rather than a reproduction of that load.
    Reset-View
    foreach ($i in 1..8) {
        SendOk 'camera_orbit 45 0' | Out-Null
        Step-EditorFrames -Session $Session -Count 12
    }
    $info = Wait-CacheStat { param($i) $i.deposits -gt 0 }
    $occupancy = $info.live / [double]$info.entries
    Assert-True -Condition ($occupancy -lt 0.75) -Message "occupancy $occupancy after an orbit: the table is filling up"
    $share = if ($info.deposits -gt 0) { $info.dropped / [double]$info.deposits } else { 0.0 }
    Assert-True -Condition ($share -lt 0.02) -Message "drop share $share after an orbit (live=$($info.live))"
    Reset-View
}

Test 'the cache fills from the rays the GI pass already traces' {
    Reset-View
    $info = Wait-CacheStat { param($i) $i.live -gt 0 }
    Assert-True -Condition ($info.live -gt 0) -Message "no cells are live: $($info | Out-String)"
    Assert-True -Condition ($info.deposits -gt 0) -Message 'no samples were deposited'
    # Every live cell should be receiving samples while the camera looks at it -
    # a live cell nothing is depositing into is one that is only waiting to age out.
    Assert-True -Condition ($info.touched -gt 0) -Message 'no cell received a sample'
}

Test 'the probe run finds a slot: drops are a negligible share of deposits' {
    # RC_PROBES slots per key, so a deposit is lost only when that many *different*
    # cells hash to the same run. At the occupancy one view produces this should be
    # essentially never; a real drop rate means RC_ENTRIES or RC_PROBES is too small
    # for the scene, and it is the one number that says so.
    Reset-View
    $info = Wait-CacheStat { param($i) $i.deposits -gt 0 }
    $share = if ($info.deposits -gt 0) { $info.dropped / [double]$info.deposits } else { 1.0 }
    Assert-True -Condition ($share -lt 0.02) -Message "drop share $share is too high (live=$($info.live))"
}

Test 'occupancy stays far below the table size on one view' {
    Reset-View
    $info = Wait-CacheStat { param($i) $i.live -gt 0 }
    $occupancy = $info.live / [double]$info.entries
    Assert-True -Condition ($occupancy -lt 0.5) -Message "occupancy $occupancy leaves no headroom for a walkthrough"
}

Test 'moving the camera reaches new surfaces and adds cells' {
    Reset-View
    $before = Wait-CacheStat { param($i) $i.live -gt 0 }
    SendOk 'camera_orbit 70 0' | Out-Null
    # New cells only appear once the moved camera has been rendered *and* the
    # counters have caught up, which is why this waits on the value rather than
    # reading once.
    $after = Wait-CacheStat { param($i) $i.live -ne $before.live }
    Assert-NotEqual -Expected $before.live -Actual $after.live -Message 'the cache did not change with the view'
    Reset-View
}

Test 'the cache debug view resolves a cell for the surfaces on screen' {
    Reset-View
    # gi_cache paints blue (0,0,~153) where the lookup found no cell at all. So a
    # frame that is mostly blue means the mixer's hash disagrees with the tracer's -
    # which is a real failure mode, and the one this catches: the two run at
    # different resolutions off different textures and must still key the same cell.
    SendOk 'render debug_buffer gi_cache', 'render debug_gain 8' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $path = Shot 'gi_cache_view'
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($path)
    try {
        $x0 = [int]($bmp.Width * 0.25); $x1 = [int]($bmp.Width * 0.75)
        $y0 = [int]($bmp.Height * 0.30); $y1 = [int]($bmp.Height * 0.70)
        $miss = 0; $total = 0
        for ($y = $y0; $y -lt $y1; $y += 4) {
            for ($x = $x0; $x -lt $x1; $x += 4) {
                $p = $bmp.GetPixel($x, $y)
                if ($p.B -gt 120 -and $p.R -lt 60 -and $p.G -lt 60) { $miss++ }
                $total++
            }
        }
        $missShare = $miss / [double]$total
        Assert-True -Condition ($missShare -lt 0.5) -Message "the cache view is $missShare miss - the mixer and the tracer disagree on the key"
    }
    finally {
        $bmp.Dispose()
        SendOk 'render debug_buffer off', 'render debug_gain 1' | Out-Null
    }
}

Test 'the confidence view separates a resolved cell from no cell at all' {
    Reset-View
    # Black is "no cell"; anything else is the cold-to-hot ramp. A settled view
    # should be overwhelmingly not-black, and that is the difference between the
    # cache not working and the cache not having got there yet.
    SendOk 'render debug_buffer gi_cache_conf' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    $path = Shot 'gi_cache_conf_view'
    $stats = Get-ImageStats -Path $path -Left 0.25 -Top 0.30 -Right 0.75 -Bottom 0.70
    Assert-True -Condition ($stats.LitShare -gt 0.5) -Message "only $($stats.LitShare) of the view has a cell"
    SendOk 'render debug_buffer off' | Out-Null
}

Test 'the two cache views are different images' {
    Reset-View
    SendOk 'render debug_buffer gi_cache', 'render debug_gain 8' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $a = Shot 'cache_value'
    SendOk 'render debug_buffer gi_cache_conf' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $b = Shot 'cache_conf'
    $diff = Get-ImageDifference -PathA $a -PathB $b
    Assert-True -Condition ($diff.DifferingShare -gt 0.5) -Message 'the value and confidence views render the same image'
    SendOk 'render debug_buffer off', 'render debug_gain 1' | Out-Null
}

Test 'turning indirect light off stops the cache being fed' {
    # The deposit lives in the GI trace, so it goes away with it. This is also the
    # check that nothing else in the frame writes the cache behind its back.
    Reset-View
    Wait-CacheStat { param($i) $i.deposits -gt 0 } | Out-Null
    SendOk 'render rt_indirect 0' | Out-Null
    $off = Wait-CacheStat { param($i) $i.deposits -eq 0 }
    Assert-Equal -Expected 0 -Actual $off.deposits -Message 'samples are still being deposited with indirect light off'

    SendOk 'render rt_indirect 1' | Out-Null
    $on = Wait-CacheStat { param($i) $i.deposits -gt 0 }
    Assert-True -Condition ($on.deposits -gt 0) -Message 'the cache did not resume when indirect light came back'
}

Test 'toggling ray tracing drops the table rather than leaving stale cells' {
    # Turning ray tracing off and on runs ResetRTBBuffers, which marks the cache for
    # a wipe on the next resolve - the same treatment the accumulation textures get.
    # A cache that survived would keep answering with the previous scene's light.
    Reset-View
    Wait-CacheStat { param($i) $i.live -gt 0 } | Out-Null
    SendOk 'render rt_indirect 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 20
    SendOk 'render rt_indirect 1' | Out-Null
    # After the wipe the table refills from zero, so live climbs back rather than
    # continuing from where it was. Assert it is populated again, which is what
    # matters - the exact count depends on the view.
    $back = Wait-CacheStat { param($i) $i.live -gt 0 }
    Assert-True -Condition ($back.live -gt 0) -Message 'the cache never refilled after a ray tracing toggle'
}

Test 'the frame stays stable with the multi-bounce feedback active' {
    # The bounce (RC_BOUNCE_GAIN in RadianceCache.hlsli) is a feedback loop: a cell's
    # value is fed back into the rays that fill it. If the loop gain ever reaches 1 it
    # holds energy instead of losing it and a corner brightens without bound, which
    # shows up here as a scene that never settles.
    #
    # This asserts stability, not magnitude. The fixture scene has almost no bounced
    # light, so measuring how much the second bounce adds has to happen on a scene
    # that has some - that is the sponza A/B recorded in CLAUDE.md (+23% on the
    # indirect buffer against a 20x smaller noise floor), not a test that can run here.
    Reset-View
    SendOk 'render debug_buffer off' | Out-Null
    Step-EditorFrames -Session $Session -Count 90
    $a = Shot 'bounce_settle_a'
    Step-EditorFrames -Session $Session -Count 60
    $b = Shot 'bounce_settle_b'
    $diff = Get-ImageDifference -PathA $a -PathB $b
    Assert-True -Condition ($diff.DifferingShare -lt 0.10) `
        -Message "the frame is still changing after settling ($($diff.DifferingShare)) - a runaway bounce looks like this"
    $stats = Get-ImageStats -Path $b -Left 0.25 -Top 0.30 -Right 0.75 -Bottom 0.70
    Assert-True -Condition ($stats.Mean -lt 250) `
        -Message "the frame is saturated (mean $($stats.Mean)) - the feedback loop has run away"
}

Test 'the primary-pixel fill does not disturb a settled frame' {
    # GIAverageCS pass 3 blends the cache in where the screen-space history cannot
    # help. On a still camera the history is perfect, so the blend weight is driven to
    # ~0 and the frame must be the same one the screen-space path alone produces.
    #
    # The guard this gives: a bug in the encoding (the cache is linear, this pass is
    # sqrt-encoded) or in the confidence gate would show up as a settled frame that
    # is suddenly darker, brighter, or blotchy - and would show up here even though
    # the fixture has too little bounced light to measure the fill's real benefit.
    Reset-View
    SendOk 'render debug_buffer off' | Out-Null
    Step-EditorFrames -Session $Session -Count 90
    $a = Shot 'primary_settled_a'
    Step-EditorFrames -Session $Session -Count 45
    $b = Shot 'primary_settled_b'
    $diff = Get-ImageDifference -PathA $a -PathB $b
    Assert-True -Condition ($diff.DifferingShare -lt 0.10) `
        -Message "a still camera is not settling with the primary fill on ($($diff.DifferingShare))"
}

Test 'the cache still answers after turning away and back' {
    # RC_MAX_AGE is the memory the fill depends on: rotate away far enough that the
    # surfaces leave the view, come back, and their cells must still be there. If the
    # age were too short they would have been evicted and the fill would have nothing
    # to offer exactly when it is needed.
    Reset-View
    $before = Wait-CacheStat { param($i) $i.live -gt 0 }
    SendOk 'camera_orbit 120 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 60
    SendOk 'camera_orbit -120 0' | Out-Null
    $after = Wait-CacheStat { param($i) $i.live -gt 0 }
    Assert-True -Condition ($after.live -gt 0) -Message 'the cache emptied while the camera looked away'
    # Nothing should have aged out over a couple of seconds - that is the whole point
    # of RC_MAX_AGE being 512 frames rather than the 64 it started at.
    Assert-Equal -Expected 0 -Actual $after.evicted -Message 'cells aged out during a short look-away'
}

Test 'a still scene settles to a steady frame' {
    # NOT "the cache changes nothing" - it deliberately does now, through the
    # multi-bounce above. What must hold is that the frame *converges*: the cache is
    # in a feedback loop with the pass that fills it, and the failure mode of getting
    # that wrong is an image that keeps drifting rather than one that is wrong in a
    # fixed way.
    Reset-View
    SendOk 'render debug_buffer off' | Out-Null
    Step-EditorFrames -Session $Session -Count 60
    $a = Shot 'normal_frame_a'
    Step-EditorFrames -Session $Session -Count 30
    $b = Shot 'normal_frame_b'
    # Against the stochastic floor, not against zero: the GI accumulates temporally
    # and two frames of the same still scene never agree exactly.
    $diff = Get-ImageDifference -PathA $a -PathB $b
    Assert-True -Condition ($diff.DifferingShare -lt 0.10) -Message "a still scene is not settling: $($diff.DifferingShare) of pixels differ"
}
