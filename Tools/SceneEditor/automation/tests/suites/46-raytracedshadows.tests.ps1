# fixture: empty
# description: Ray traced point light shadows - a point light with raytraced_shadow holds no shadow map; the deferred lighting pass traces one ray from each lit surface to the light and drops the light where something is in the way. Exclusive with cast_shadow, packed as a flag, follows a moving caster, and agrees with the shadow map it replaces.

# One light above a 60 x 60 floor with a flat box between them. The same light is lit three ways -
# unshadowed, shadow map, ray traced - and the pictures are compared: the ray traced shadow must be
# there (the picture differs from the unshadowed one), must be the same shadow the map draws, and
# must follow the box when it moves (the scene the rays go through is rebuilt every frame).
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on
# the viewport).

function Get-LightsInfo {
    $r = (SendOk 'lights_info')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]@{
        Lights = [int]$o['lights']; Shadowed = [int]$o['shadowed']; RayTraced = [int]$o['raytraced']
        Objects = [int]$o['shadow_ray_objects']; Valid = ($o['valid'] -eq '1')
    }
}

# The counters are read back a few frames late: step until the one we wait for shows up.
function Wait-LightsInfo {
    param([int]$Lights, [int]$RayTraced, [int]$Shadowed)
    for ($i = 0; $i -lt 30; $i++) {
        Step-EditorFrames -Session $Session -Count 5
        $info = Get-LightsInfo
        if ($info.Lights -eq $Lights -and $info.RayTraced -eq $RayTraced -and $info.Shadowed -eq $Shadowed) {
            Step-EditorFrames -Session $Session -Count 10
            return Get-LightsInfo
        }
    }
    throw "lights_info never reported lights=$Lights raytraced=$RayTraced shadowed=$Shadowed (last: $((SendOk 'lights_info')[0].Text))"
}

function Get-SceneShot {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 12
    $path = Shot $Name
    return [pscustomobject]@{ Path = $path; Stats = (Get-ImageStats -Path $path -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3) }
}

function Get-SceneDifference {
    param($A, $B)
    return (Get-ImageDifference -PathA $A.Path -PathB $B.Path -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3 -Threshold 6)
}

Test 'set up: a floor, a flat box over it, one lamp over the box, no sun' {
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 60 0.2 60' | Out-Null
    SendOk 'select box_b',
        "set_component box_b Physics ""{'type':'STATIC'}""",
        'set_position 0 4 0', 'set_scale 10 0.5 10', 'set_rotation 0 0 0' | Out-Null
    # The fixture's third box, well out of the lamp's reach: it is not part of this scene.
    SendOk 'select box_c', 'set_position 300 4 300' | Out-Null
    SendOk "set_component sun DirectionalLight ""{'intensity':0}""",
        "set_component ambient AmbientLight ""{'color_down':{'x':0,'y':0,'z':0},'color_up':{'x':0,'y':0,'z':0}}""" | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk 'select box_a', 'focus' | Out-Null
    SendOk 'camera_pos 0 38 -34' | Out-Null
    SendOk 'light_gizmos off', 'light_positions off' | Out-Null
    SendOk 'spawn_lights 1 40 6 0 12' | Out-Null
    $info = Wait-LightsInfo -Lights 1 -RayTraced 0 -Shadowed 0
    Assert-Equal -Expected 1 -Actual $info.Lights -Message 'the lamp is packed'
    Assert-Equal -Expected 0 -Actual $info.Objects -Message 'no ray traced light, so no shadow scene is built'
}

Test 'raytraced_shadow and cast_shadow are exclusive, in either order' {
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'tl_0' -Component 'PointLight'
    Assert-True -Condition ([bool]$c.raytraced_shadow) -Message 'the ray traced shadow is on'
    Assert-True -Condition (-not [bool]$c.cast_shadow) -Message 'and the shadow map is off'
    SendOk "set_component tl_0 PointLight ""{'cast_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'tl_0' -Component 'PointLight'
    Assert-True -Condition ([bool]$c.cast_shadow) -Message 'the shadow map is on'
    Assert-True -Condition (-not [bool]$c.raytraced_shadow) -Message 'and turning it on turned the ray traced shadow off'
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'tl_0' -Component 'PointLight'
    Assert-True -Condition ([bool]$c.raytraced_shadow -and -not [bool]$c.cast_shadow) -Message 'and back again'
}

Test 'a ray traced light holds no shadow slot, and is packed with the ray traced flag' {
    $info = Wait-LightsInfo -Lights 1 -RayTraced 1 -Shadowed 0
    Assert-Equal -Expected 0 -Actual $info.Shadowed -Message 'no shadow map slot'
    Assert-Equal -Expected 1 -Actual $info.RayTraced -Message 'one ray traced light'
    Assert-True -Condition ($info.Objects -ge 2) -Message "the scene the rays go through holds the floor and the box ($($info.Objects) objects)"
    $r = (SendOk 'lights_packed 0')[0].Text
    Assert-Match -Pattern 'slot=-1 rt=1' -Actual $r -Message "packed ray traced, no slot ($r)"
}

Test 'the ray traced shadow is there: the floor under the box is darker than without it' {
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':false,'cast_shadow':false}""" | Out-Null
    Wait-LightsInfo -Lights 1 -RayTraced 0 -Shadowed 0 | Out-Null
    $script:open = Get-SceneShot 'unshadowed'
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    Wait-LightsInfo -Lights 1 -RayTraced 1 -Shadowed 0 | Out-Null
    $script:traced = Get-SceneShot 'raytraced'
    $d = Get-SceneDifference $script:open $script:traced
    Assert-True -Condition ($d.DifferingShare -gt 0.02) `
        -Message ("{0:P2} of the pixels differ from the unshadowed picture" -f $d.DifferingShare)
    Assert-True -Condition ($script:traced.Stats.Mean -lt $script:open.Stats.Mean) `
        -Message ("the scene is darker: {0:N2} against {1:N2}" -f $script:traced.Stats.Mean, $script:open.Stats.Mean)
}

Test 'the ray traced shadow is the one the shadow map draws' {
    SendOk "set_component tl_0 PointLight ""{'cast_shadow':true}""" | Out-Null
    $info = Wait-LightsInfo -Lights 1 -RayTraced 0 -Shadowed 1
    Assert-Equal -Expected 0 -Actual $info.RayTraced -Message 'the shadow map replaced the traced shadow'
    $mapped = Get-SceneShot 'shadowmap'
    $vsNone = Get-SceneDifference $script:open $mapped
    $vsRay = Get-SceneDifference $script:traced $mapped
    Write-Host ("    shadow map vs unshadowed: mean delta {0:N2}; shadow map vs ray traced: {1:N2}" -f $vsNone.MeanDelta, $vsRay.MeanDelta)
    Assert-True -Condition ($vsRay.MeanDelta -lt $vsNone.MeanDelta * 0.5) `
        -Message ("the ray traced picture is much nearer the shadow map ({0:N2}) than the unshadowed one is ({1:N2})" -f $vsRay.MeanDelta, $vsNone.MeanDelta)
}

Test 'the shadow follows the box: move it out of the way and the light is back' {
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    $script:objectsWithBox = (Wait-LightsInfo -Lights 1 -RayTraced 1 -Shadowed 0).Objects
    # The box leaves the picture as well as the light's way, so what it is compared with is the
    # same empty floor lit with no shadow at all.
    SendOk 'select box_b', 'set_position 40 4 40' | Out-Null
    $moved = Get-SceneShot 'box-away'
    $info = Wait-LightsInfo -Lights 1 -RayTraced 1 -Shadowed 0
    Assert-True -Condition ($info.Objects -lt $script:objectsWithBox) -Message "the box left the shadow scene ($($info.Objects) objects, $($script:objectsWithBox) with it)"
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':false}""" | Out-Null
    Wait-LightsInfo -Lights 1 -RayTraced 0 -Shadowed 0 | Out-Null
    $movedOpen = Get-SceneShot 'box-away-unshadowed'
    $d = Get-SceneDifference $movedOpen $moved
    # Not 0: the jittered rays leave a little noise along the thin floor's edges. A real shadow is 12%.
    Assert-True -Condition ($d.DifferingShare -lt 0.02) `
        -Message ("with the box out of the way the traced picture is the unshadowed one ({0:P2} differ)" -f $d.DifferingShare)
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    SendOk 'select box_b', 'set_position 0 4 0' | Out-Null
    $back = Get-SceneShot 'box-back'
    $d2 = Get-SceneDifference $script:traced $back
    # The edge is jittered per frame, so two frames differ along it - a few percent, not the whole shadow.
    Assert-True -Condition ($d2.DifferingShare -lt 0.04) `
        -Message ("and with the box back the shadow is too ({0:P2} differ from the first traced picture)" -f $d2.DifferingShare)
}

Test 'the jittered edge does not depend on the frame: two frames in a row are the same picture' {
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    Wait-LightsInfo -Lights 1 -RayTraced 1 -Shadowed 0 | Out-Null
    # The aim of a ray is a pattern over a 4x4 tile of pixels (ShadowRays.hlsli) and the blur of ShadowAccumulateCS spans the tile,
    # so nothing moves from one frame to the next: a still scene is a still picture, however little the frames are averaged.
    foreach ($blend in @('1', '0.08')) {
        SendOk ("shadow_rays 1 {0}" -f $blend) | Out-Null
        Step-EditorFrames -Session $Session -Count 60
        $a = Get-SceneShot ("still-a-$blend")
        $b = Get-SceneShot ("still-b-$blend")
        $d = Get-SceneDifference $a $b
        Write-Host ("    blend {0}: two frames apart {1:P3} of the pixels differ (mean delta {2:N3})" -f $blend, $d.DifferingShare, $d.MeanDelta)
        Assert-True -Condition ($d.MeanDelta -lt 0.5) -Message ("the edge is steady with blend {0} (mean delta {1:N3})" -f $blend, $d.MeanDelta)
    }
}

Test 'the ray traced flag is part of the serialized component' {
    $c = Get-Component -Session $Session -Entity 'tl_0' -Component 'PointLight'
    Assert-True -Condition ([bool]$c.raytraced_shadow) -Message 'raytraced_shadow is in the serialized component'
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

Test 'no ray traced light left: the shadow scene is gone' {
    SendOk 'clear_lights' | Out-Null
    $info = Wait-LightsInfo -Lights 0 -RayTraced 0 -Shadowed 0
    Assert-Equal -Expected 0 -Actual $info.Objects -Message 'nothing is gathered for rays that nobody traces'
}
