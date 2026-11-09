# fixture: empty
# description: The frame graph (Engine/Core/FrameGraph.h): the frame described as passes and the frame textures each of them uses, the transient textures sharing the pool's memory over the passes they live through, the persistent ones (histories) made when their feature first runs and given back once it has been off for a while, and no pass using a transient outside its passes.

# The last frame's graph: one line of numbers (`frame_graph`), as a hashtable of key -> number.
function Get-FrameGraph {
    param([switch]$Describe)
    $command = if ($Describe) { 'frame_graph describe' } else { 'frame_graph' }
    $r = Invoke-EditorCommand -Session $Session -Command $command
    Assert-Ok -Result $r[0]
    $values = @{}
    foreach ($pair in ($r[0].Text -replace '^OK\s*', '' -split '\s+')) {
        $kv = $pair -split '=', 2
        if ($kv.Count -eq 2) { $values[$kv[0]] = [double]$kv[1] }
    }
    $values['lines'] = @($r[0].Payload)
    return $values
}

# The pass lines of `frame_graph describe`, in order: "pass <n> <name>[ (culled)]: r:... w:...".
function Get-PassNames {
    param($Graph)
    return @($Graph.lines | Where-Object { $_ -like 'pass *' } | ForEach-Object {
        ($_ -replace '^pass \d+ ', '' -replace ':.*$', '' -replace ' \(culled\)$', '')
    })
}

Test 'frame_graph reports the last frame: its passes, its transient textures and the pool textures they were given' {
    Step-EditorFrames -Session $Session -Count 5
    $g = Get-FrameGraph
    foreach ($key in 'passes', 'culled', 'transient', 'physical', 'transient_mb', 'physical_mb', 'aliased_mb', 'persistent', 'persistent_mb',
                     'idle', 'idle_mb', 'pool', 'pool_mb', 'clears_done', 'clears_dropped', 'violations', 'draws', 'descriptors_copied') {
        Assert-True -Condition ($g.ContainsKey($key)) -Message "the line has $key"
    }
    Assert-True -Condition ($g.passes -ge 10) -Message "the scene and the post-process chain are passes (got $($g.passes))"
    Assert-True -Condition ($g.transient -ge 1) -Message 'the frame has transient textures'
    Assert-True -Condition ($g.physical -ge 1 -and $g.physical -le $g.transient) -Message "no more pool textures than transients ($($g.physical) for $($g.transient))"
    Assert-True -Condition ($g.physical_mb -le $g.transient_mb + 0.01) -Message 'sharing never costs more than a texture each'
    Assert-True -Condition ($g.aliased_mb -le $g.physical_mb + 0.01) -Message 'what a heap shared by every kind would take is no more than the pool'
    Assert-True -Condition ($g.pool -ge $g.physical) -Message 'the pool holds at least what the frame used'
    Assert-Equal -Expected 0 -Actual $g.violations -Message 'every pass declares the transients it uses'
}

Test 'the passes are the frame in the order it is recorded, the post-process chain a pass per stage at the end' {
    $names = Get-PassNames -Graph (Get-FrameGraph -Describe)
    foreach ($name in 'depth', 'sky', 'scene', 'lighting', 'mix', 'anti-aliasing', 'post begin', 'post end') {
        Assert-Contains -Collection $names -Value $name -Message "a pass '$name'"
    }
    $order = 'depth', 'scene', 'lighting', 'mix', 'anti-aliasing', 'post begin', 'post end' | ForEach-Object { [Array]::IndexOf($names, $_) }
    for ($i = 1; $i -lt $order.Count; $i++) {
        Assert-True -Condition ($order[$i] -gt $order[$i - 1]) -Message "the passes are in the order of the frame ($($names -join ', '))"
    }
    $stages = @($names | Where-Object { $_ -like 'post *' -and $_ -ne 'post begin' -and $_ -ne 'post end' })
    Assert-True -Condition ($stages.Count -ge 2) -Message "one pass per post-process stage (got $($stages -join ', '))"
}

Test 'textures of one kind whose passes do not overlap share a pool texture' {
    $g = Get-FrameGraph -Describe
    # "texture <name> transient passes <a>..<b> slot <s> <w>x<h> fmt <f> <mb> MB"
    $textures = @($g.lines | Where-Object { $_ -like 'texture *' } | ForEach-Object {
        if ($_ -match '^texture (.+) (transient|persistent) passes (-?\d+)\.\.(-?\d+) slot (\d+) (\d+)x(\d+) fmt (\d+)') {
            [pscustomobject]@{ Name = $Matches[1]; First = [int]$Matches[3]; Last = [int]$Matches[4]; Slot = [int]$Matches[5]
                Kind = "$($Matches[6])x$($Matches[7]) $($Matches[8])" }
        }
    })
    Assert-True -Condition ($textures.Count -eq $g.transient) -Message 'a line per transient texture'
    # Two textures on the same pool texture never live through the same pass.
    foreach ($a in $textures) {
        foreach ($b in $textures) {
            if ($a.Name -ge $b.Name -or $a.Kind -ne $b.Kind -or $a.Slot -ne $b.Slot) { continue }
            Assert-True -Condition ($a.Last -lt $b.First -or $b.Last -lt $a.First) -Message "$($a.Name) and $($b.Name) share a texture but overlap"
        }
    }
    Assert-True -Condition ($g.physical -lt $g.transient) -Message "the post-process chain's stages share (got $($g.physical) pool textures for $($g.transient) transients)"
}

Test 'a feature that is off costs its history textures nothing: GTAO is made when it runs and given back after it stops' {
    SendOk 'render gtao 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $on = Get-FrameGraph -Describe
    $gtao = @($on.lines | Where-Object { $_ -like 'texture gtao *' })
    Assert-True -Condition ($gtao.Count -eq 1) -Message 'the GTAO pass has its transient while it runs'
    SendOk 'render gtao 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $off = Get-FrameGraph -Describe
    Assert-True -Condition (@($off.lines | Where-Object { $_ -like 'texture gtao *' }).Count -eq 0) -Message 'and none once it is off'
    # The two history textures stay a while (a feature switched back on finds them), then go.
    Step-EditorFrames -Session $Session -Count 300
    $later = Get-FrameGraph
    Assert-True -Condition ($later.idle -ge $on.idle + 2) -Message "the GTAO histories are given back (idle $($on.idle) -> $($later.idle))"
    Assert-True -Condition ($later.persistent_mb -lt $on.persistent_mb) -Message "and their memory with them ($($on.persistent_mb) -> $($later.persistent_mb) MB)"
    Assert-Equal -Expected 0 -Actual $later.violations
}

Test 'debug views, ray tracing qualities and the temporal filters use their transients only inside their passes' {
    $steps = 'render debug_buffer shadow_bvh', 'render debug_buffer off', 'render rt_quality low', 'render rt_quality high', 'render gtao 1',
             'render debug_buffer light', 'render debug_buffer off', 'render gtao 0'
    foreach ($step in $steps) {
        SendOk $step | Out-Null
        Step-EditorFrames -Session $Session -Count 4
    }
    $g = Get-FrameGraph
    Assert-Equal -Expected 0 -Actual $g.violations -Message 'no pass used a transient it did not declare'
}
