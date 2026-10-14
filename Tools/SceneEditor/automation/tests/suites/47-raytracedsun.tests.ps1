# fixture: empty
# description: Ray traced directional light shadows - a sun with raytraced_shadow renders no cascade maps; the deferred lighting pass traces a ray from each lit surface towards the sun and drops the sun's light where something is in the way. Exclusive with cast_shadow, follows a moving caster, and agrees with the cascaded maps it replaces.

# The same sun is lit three ways over a floor with a box above it - unshadowed, cascaded maps, ray
# traced - and the pictures are compared. The ray traced one must differ from the unshadowed one (the
# shadow is there) and be the shadow the cascades draw. Only x 0.42-0.78 of the window is scene.

function Get-LightsInfo {
    $r = (SendOk 'lights_info')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]@{ RayTracedDir = [int]$o['raytraced_dir']; Objects = [int]$o['shadow_ray_objects'] }
}

function Wait-RayTracedSun {
    param([int]$Count)
    for ($i = 0; $i -lt 30; $i++) {
        Step-EditorFrames -Session $Session -Count 5
        $info = Get-LightsInfo
        if ($info.RayTracedDir -eq $Count) {
            Step-EditorFrames -Session $Session -Count 40
            return Get-LightsInfo
        }
    }
    throw "lights_info never reported raytraced_dir=$Count (last: $((SendOk 'lights_info')[0].Text))"
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

Test 'set up: a floor, a flat box over it, a slanted sun (its direction points TO the light), no ambient light' {
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 60 0.2 60' | Out-Null
    SendOk 'select box_b',
        "set_component box_b Physics ""{'type':'STATIC'}""",
        'set_position 0 6 0', 'set_scale 10 0.5 10', 'set_rotation 0 0 0' | Out-Null
    SendOk 'select box_c', 'set_position 300 6 300' | Out-Null
    SendOk "set_component ambient AmbientLight ""{'color_down':{'x':0,'y':0,'z':0},'color_up':{'x':0,'y':0,'z':0}}""" | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk "set_component sun DirectionalLight ""{'intensity':2,'direction':{'x':0.35,'y':1,'z':0.25},'cast_shadow':false,'raytraced_shadow':false,'shadow_distance':80}""" | Out-Null
    SendOk 'select box_a', 'focus' | Out-Null
    SendOk 'camera_pos 0 38 -34' | Out-Null
    SendOk 'light_gizmos off', 'light_positions off' | Out-Null
    $info = Wait-RayTracedSun -Count 0
    Assert-Equal -Expected 0 -Actual $info.Objects -Message 'no ray traced light, so no shadow scene is built'
}

Test 'raytraced_shadow and cast_shadow are exclusive on the sun, in either order' {
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'sun' -Component 'DirectionalLight'
    Assert-True -Condition ([bool]$c.raytraced_shadow -and -not [bool]$c.cast_shadow) -Message 'ray traced on, cascades off'
    SendOk "set_component sun DirectionalLight ""{'cast_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'sun' -Component 'DirectionalLight'
    Assert-True -Condition ([bool]$c.cast_shadow -and -not [bool]$c.raytraced_shadow) -Message 'turning the cascades on turned the ray traced shadow off'
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    $c = Get-Component -Session $Session -Entity 'sun' -Component 'DirectionalLight'
    Assert-True -Condition ([bool]$c.raytraced_shadow -and -not [bool]$c.cast_shadow) -Message 'and back again'
}

Test 'a ray traced sun builds a shadow scene round the camera' {
    $info = Wait-RayTracedSun -Count 1
    Assert-Equal -Expected 1 -Actual $info.RayTracedDir -Message 'one ray traced sun'
    Assert-True -Condition ($info.Objects -ge 2) -Message "the scene holds the floor and the box ($($info.Objects) objects)"
}

Test 'the ray traced sun shadow is there, and is the one the cascades draw' {
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':false,'cast_shadow':false}""" | Out-Null
    Wait-RayTracedSun -Count 0 | Out-Null
    $script:open = Get-SceneShot 'sun-unshadowed'
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    Wait-RayTracedSun -Count 1 | Out-Null
    $script:traced = Get-SceneShot 'sun-raytraced'
    SendOk "set_component sun DirectionalLight ""{'cast_shadow':true}""" | Out-Null
    Wait-RayTracedSun -Count 0 | Out-Null
    $mapped = Get-SceneShot 'sun-cascades'
    $vsNone = Get-SceneDifference $script:open $script:traced
    $vsMap = Get-SceneDifference $script:traced $mapped
    $mapVsNone = Get-SceneDifference $script:open $mapped
    Write-Host ("    ray traced vs unshadowed: {0:N2}; ray traced vs cascades: {1:N2}; cascades vs unshadowed: {2:N2}" -f $vsNone.MeanDelta, $vsMap.MeanDelta, $mapVsNone.MeanDelta)
    Assert-True -Condition ($vsNone.DifferingShare -gt 0.02) -Message ("the shadow is there ({0:P2} of the pixels differ from the unshadowed picture)" -f $vsNone.DifferingShare)
    Assert-True -Condition ($script:traced.Stats.Mean -lt $script:open.Stats.Mean) -Message 'the scene is darker with the shadow'
    Assert-True -Condition ($vsMap.MeanDelta -lt $mapVsNone.MeanDelta * 0.5) `
        -Message ("it is nearer the cascades' shadow ({0:N2}) than the unshadowed picture is ({1:N2})" -f $vsMap.MeanDelta, $mapVsNone.MeanDelta)
}

Test 'the sun shadow follows the box: move it away and the light is back' {
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    Wait-RayTracedSun -Count 1 | Out-Null
    SendOk 'select box_b', 'set_position 200 6 200' | Out-Null
    $moved = Get-SceneShot 'sun-box-away'
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':false}""" | Out-Null
    Wait-RayTracedSun -Count 0 | Out-Null
    $movedOpen = Get-SceneShot 'sun-box-away-unshadowed'
    $d = Get-SceneDifference $movedOpen $moved
    Assert-True -Condition ($d.DifferingShare -lt 0.02) `
        -Message ("with the box out of the way the traced picture is the unshadowed one ({0:P2} differ)" -f $d.DifferingShare)
    SendOk 'select box_b', 'set_position 0 6 0' | Out-Null
    SendOk "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    Wait-RayTracedSun -Count 1 | Out-Null
    $back = Get-SceneShot 'sun-box-back'
    $d2 = Get-SceneDifference $script:traced $back
    Assert-True -Condition ($d2.DifferingShare -lt 0.04) `
        -Message ("and with the box back the shadow is too ({0:P2} differ from the first traced picture)" -f $d2.DifferingShare)
}

Test 'a ray traced sun beside a ray traced lamp: both trace, nothing crashes' {
    SendOk 'spawn_lights 1 40 6 0 14' | Out-Null
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    $r = (SendOk 'lights_info')[0].Text
    Assert-Match -Pattern 'raytraced=1 raytraced_dir=1' -Actual $r -Message "one lamp and one sun trace ($r)"
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
    SendOk 'clear_lights' | Out-Null
}
