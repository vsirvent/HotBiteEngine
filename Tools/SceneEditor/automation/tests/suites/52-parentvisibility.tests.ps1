# fixture: empty
# description: A child is only visible if its parent is - hiding an entity hides everything parented to it (meshes and lights) without touching the children's own Base.visible, and showing it again brings back exactly the children that were visible.

# The rule lives in Components::IsShown (Base.h): `visible` AND the `visible` of every ancestor.
# Everything that decides what is drawn, lit or animated asks that instead of Base.visible, so
# the checks here are on the two ends of the pipe that prove it reached them:
#
#  * a MESH, by what the frame shows. The view is the `depth` debug buffer rather than the lit
#    frame: an empty level has no light to read a cube by, depth does not care. The child is
#    parked at the origin and the PARENT far off to the side, so the parent's own cube never
#    reaches the measured rectangle - what changes when the parent is hidden is the child alone.
#  * a LIGHT, by `lights_info`: the number of lights the frame collected. A point light hanging
#    off a hidden parent must drop out of it, with its own `visible` still true.
#
# Base.visible itself is asserted untouched after each hide, because "hide the children too by
# clearing their flags" would pass every picture here and lose the children's own state - the
# parent coming back could no longer tell a child that was hidden on purpose from one that was
# hidden by the parent.

$Away = '40 0 0'

function New-Cube {
    param([string]$Position)
    $r = SendOk 'menu "Add/Mesh Object"'
    $name = ($r[0].Text -replace '^Created [^:]+:\s*', '').Trim()
    SendOk "select $name", "set_position $Position" | Out-Null
    return $name
}

function Get-OwnVisible {
    param([string]$Entity)
    return [bool](Get-Component -Session $Session -Entity $Entity -Component 'Base').visible
}

function Set-Visible {
    param([string]$Entity, [bool]$Visible)
    $v = if ($Visible) { 'true' } else { 'false' }
    SendOk "set_component $Entity Base ""{'visible':$v}""" | Out-Null
}

# The share of the measured rectangle the depth buffer paints, which is the child cube: at 8 units
# a cube scaled to 3 reads about 16/255 on the front face, past the 8 the lit test counts.
function Get-ChildShare {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 6
    return (Get-ImageStats -Path (Shot $Name) -Left 0.45 -Top 0.30 -Right 0.75 -Bottom 0.75).LitShare
}

function Get-LightCount {
    return [int](([regex]::Match((SendOk 'lights_info')[0].Text, 'lights=(\d+)')).Groups[1].Value)
}

# lights_info is read back a few frames late, so step until two reads agree.
function Get-SettledLightCount {
    $last = -1
    for ($i = 0; $i -lt 30; $i++) {
        Step-EditorFrames -Session $Session -Count 8
        $now = Get-LightCount
        if ($now -eq $last) { return $now }
        $last = $now
    }
    throw "lights_info never settled (last: $last)"
}

Test 'a parent far from the view, and a child cube in it, are ready' {
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -8', 'camera_target 0 0 0', 'render debug_buffer depth' | Out-Null
    $script:parent = New-Cube -Position $Away
    $script:child = New-Cube -Position '0 0 0'
    SendOk "select $script:child", 'set_scale 3 3 3', 'deselect' | Out-Null
    SendOk "set_parent $script:child $script:parent" | Out-Null
    $script:shown = Get-ChildShare 'both-visible'
    Assert-True -Condition ($script:shown -gt 0.05) `
        -Message "the child cube should reach the view ($([Math]::Round($script:shown * 100, 1))% painted)"
}

Test 'hiding the parent hides the child, and leaves the child its own flag' {
    Set-Visible -Entity $script:parent -Visible $false
    $hidden = Get-ChildShare 'parent-hidden'
    Assert-True -Condition ($hidden -lt $script:shown - 0.05) `
        -Message "the child should leave the view with its parent (visible $([Math]::Round($script:shown * 100, 1))%, parent hidden $([Math]::Round($hidden * 100, 1))%)"
    Assert-True -Condition (Get-OwnVisible -Entity $script:child) -Message 'the child is still visible in its own right'
    Assert-False -Condition (Get-OwnVisible -Entity $script:parent) -Message 'and the parent is the one that is hidden'
}

Test 'showing the parent brings the child back' {
    Set-Visible -Entity $script:parent -Visible $true
    $again = Get-ChildShare 'parent-shown-again'
    Assert-True -Condition ($again -gt $script:shown - 0.02) `
        -Message "the child should be back ($([Math]::Round($again * 100, 1))% painted, was $([Math]::Round($script:shown * 100, 1))%)"
}

Test 'a visible parent does not show a child that is hidden itself' {
    # The rule is an AND: the parent's flag cannot turn a hidden child on.
    Set-Visible -Entity $script:child -Visible $false
    $off = Get-ChildShare 'child-hidden'
    Assert-True -Condition ($off -lt $script:shown - 0.05) `
        -Message "a hidden child stays hidden under a visible parent ($([Math]::Round($off * 100, 1))% painted)"
    Set-Visible -Entity $script:parent -Visible $false
    Set-Visible -Entity $script:parent -Visible $true
    $still = Get-ChildShare 'child-hidden-after-parent-toggle'
    Assert-True -Condition ($still -lt $script:shown - 0.05) `
        -Message 'toggling the parent must not bring back a child that was hidden on purpose'
    Set-Visible -Entity $script:child -Visible $true
}

Test 'a light on a hidden parent drops out of the frame' {
    SendOk 'render debug_buffer off' | Out-Null
    $before = Get-SettledLightCount
    $r = SendOk 'menu "Add/Point Light"'
    $script:light = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    SendOk "select $script:light", 'set_position 0 3 0', 'deselect' | Out-Null
    SendOk "set_parent $script:light $script:parent" | Out-Null
    $with = Get-SettledLightCount
    Assert-Equal -Expected ($before + 1) -Actual $with -Message 'the light on a visible parent is one of the frame''s lights'

    Set-Visible -Entity $script:parent -Visible $false
    Assert-Equal -Expected $before -Actual (Get-SettledLightCount) -Message 'hidden with its parent'
    Assert-True -Condition (Get-OwnVisible -Entity $script:light) -Message 'the light keeps its own flag'

    Set-Visible -Entity $script:parent -Visible $true
    Assert-Equal -Expected ($before + 1) -Actual (Get-SettledLightCount) -Message 'and back with it'
}

Test 'the rule holds down a chain: a grandchild follows its grandparent' {
    # The editor only builds one level (set_parent refuses more), but the engine walks the whole
    # chain, so a deeper one is made the way a level file or game code would: through Base.parent.
    $before = Get-SettledLightCount
    $r = SendOk 'menu "Add/Point Light"'
    $deep = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    SendOk "set_component $deep Base ""{'parent':'$script:child'}""" | Out-Null
    Assert-Equal -Expected ($before + 1) -Actual (Get-SettledLightCount) -Message 'the grandchild light is lit while the chain is visible'

    Set-Visible -Entity $script:parent -Visible $false
    Assert-Equal -Expected ($before - 1) -Actual (Get-SettledLightCount) -Message 'hiding the grandparent hides the child light and the grandchild light'
    Assert-True -Condition (Get-OwnVisible -Entity $deep) -Message 'the grandchild keeps its own flag'
    Assert-True -Condition (Get-OwnVisible -Entity $script:child) -Message 'so does the child between them'
    Set-Visible -Entity $script:parent -Visible $true
}
