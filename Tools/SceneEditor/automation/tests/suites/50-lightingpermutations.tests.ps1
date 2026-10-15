# fixture: empty
# description: The lighting shader's permutations (DeferredLightCS.hlsl) - the ordinary surfaces with the lights that need no ray, the lights that trace their shadow, the water and the lava are lit by a shader each, dispatched only when the frame has something for it, and the picture is the one the single shader that did it all draws, whichever compiler (DXC or fxc) built them.

# What is checked, and how:
#
# * WHICH PERMUTATION RAN comes off the engine: `lighting_shaders` answers with the dispatches of each permutation in the
#   last frame. A frame with no water has no water dispatch, and so on - that is what "only if the material is present" means.
# * SAME PICTURE: `lighting_shaders single` is the shader as it was before it was split. The same scene shot with it, with
#   the permutations, and with either compiler's build of both must be the same image up to the frame's own noise (the
#   picture of two shots with the same shader differs by that much, and the other pictures may differ by a few times it).
# * The water and the lava are made by giving the floor's material the engine's WaterPS / LavaPS pixel shaders, which are the
#   ones that write those shading models into the material map (what the permutations are dispatched for).
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on the viewport).

$invariant = [Globalization.CultureInfo]::InvariantCulture
function F { param([double]$Value) return $Value.ToString($invariant) }

# What the last frame dispatched: standard, rt, water, lava; and which set runs.
function Get-Lighting {
    $r = (SendOk 'lighting_shaders')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]@{ Split = $o['split']; Compiler = $o['compiler']; Standard = [int]$o['standard']; Rt = [int]$o['rt']; Water = [int]$o['water']; Lava = [int]$o['lava'] }
}

function Use-Lighting {
    param([string]$Set)
    SendOk "lighting_shaders $Set" | Out-Null
    # The pipeline of a shader used for the first time, and the temporal history.
    Step-EditorFrames -Session $Session -Count 30
}

function Get-SceneShot {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 12
    return (Shot $Name)
}

# The scene as the four sets of lighting shaders draw it, compared with the single shader's (and with itself, for the noise).
function Assert-SamePicture {
    param([string]$What, [double]$MinLit = 0.0)
    $shots = @{}
    foreach ($c in @(@('single dxc', 'single-dxc'), @('split dxc', 'split-dxc'), @('split fxc', 'split-fxc'), @('split auto', 'split-auto'), @('single fxc', 'single-fxc'), @('single dxc', 'single-dxc-again'))) {
        Use-Lighting $c[0]
        $shots[$c[1]] = Get-SceneShot ("$What-" + $c[1])
    }
    $area = @{ Left = 0.42; Top = 0.30; Right = 0.78; Bottom = 0.95; Step = 3; Threshold = 6 }
    # The same picture is worth saying only of a picture with something in it.
    $lit = (Get-ImageStats -Path $shots['single-dxc'] -Left 0.42 -Top 0.30 -Right 0.78 -Bottom 0.95 -Step 3).LitShare
    Assert-True -Condition ($lit -ge $MinLit) -Message ("{0}: the scene is lit on {1:P0} of its pixels, at least {2:P0} expected" -f $What, $lit, $MinLit)
    $noise = Get-ImageDifference -PathA $shots['single-dxc'] -PathB $shots['single-dxc-again'] @area
    foreach ($name in 'split-dxc', 'split-fxc', 'split-auto', 'single-fxc') {
        $diff = Get-ImageDifference -PathA $shots['single-dxc'] -PathB $shots[$name] @area
        Assert-True -Condition ($diff.MeanDelta -le [Math]::Max($noise.MeanDelta * 3.0, 1.0)) `
            -Message ("{0}: {1} against the single shader: mean delta {2:N2} (two shots of the single shader: {3:N2})" -f $What, $name, $diff.MeanDelta, $noise.MeanDelta)
        Assert-True -Condition ($diff.DifferingShare -le [Math]::Max($noise.DifferingShare * 3.0, 0.01)) `
            -Message ("{0}: {1} against the single shader: {2:P2} of the pixels differ by more than 6 (two shots of the single shader: {3:P2})" -f $What, $name, $diff.DifferingShare, $noise.DifferingShare)
    }
    SendOk 'lighting_shaders single auto' | Out-Null
}

Test 'set up: a floor under the sun and a few point lights' {
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 60 0.2 60' | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk 'select box_a', 'focus' | Out-Null
    SendOk 'camera_pos 0 38 -34' | Out-Null
    SendOk 'light_gizmos off', 'light_positions off' | Out-Null
    SendOk 'spawn_lights 8 22 3 40 4' | Out-Null
    Step-EditorFrames -Session $Session -Count 20
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'the scene is up'
}

Test 'ordinary surfaces: the standard permutation runs, nothing else does' {
    # The one shader that does it all is what runs unless asked otherwise (the permutations are not faster, see the migration doc).
    $default = Get-Lighting
    Assert-Equal -Expected 'no' -Actual $default.Split -Message 'the one shader is the default'
    Assert-Equal -Expected 'auto' -Actual $default.Compiler -Message 'built by whichever compiler is faster for it'
    Use-Lighting 'split dxc'
    $l = Get-Lighting
    Assert-Equal -Expected 'yes' -Actual $l.Split -Message 'split asks for the permutations'
    Assert-True -Condition ($l.Standard -ge 1) -Message 'the ordinary surfaces are lit'
    Assert-Equal -Expected 0 -Actual $l.Rt -Message 'no light traces its shadow, so no ray tracing permutation'
    Assert-Equal -Expected 0 -Actual $l.Water -Message 'no water in the frame, no water permutation'
    Assert-Equal -Expected 0 -Actual $l.Lava -Message 'no lava in the frame, no lava permutation'
    SendOk 'lighting_shaders single' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $s = Get-Lighting
    Assert-Equal -Expected 'no' -Actual $s.Split -Message 'single is the one shader'
    Assert-True -Condition ($s.Standard -ge 1) -Message 'and it is dispatched (counted as the standard one while nothing traces)'
    SendOk 'lighting_shaders split' | Out-Null
}

Test 'ordinary surfaces: the same picture from the permutations and the single shader, built by DXC or by fxc' {
    Assert-SamePicture 'standard'
}

Test 'water: the water permutation runs, and the picture is the single shader''s' {
    # Over a floor, and out of the depth pre-pass: the water and the lava fade by how far the surface behind them is, and they are
    # only lit where something is there (the editor draws no second geometry pass, which is where Survival's water is).
    SendOk 'select box_b', "set_component box_b Physics ""{'type':'STATIC'}""", 'set_position 0 -1.5 0', 'set_scale 80 0.2 80',
        "set_component box_a Base ""{'draw_depth':false}""", 'set_material box_a TestRed', 'set_shader TestRed draw_ps WaterPS.cso' | Out-Null
    Use-Lighting 'split dxc'
    $l = Get-Lighting
    Assert-True -Condition ($l.Water -ge 1) -Message 'a frame with water has the water permutation'
    Assert-Equal -Expected 0 -Actual $l.Lava -Message 'and no lava one'
    Assert-SamePicture 'water' -MinLit 0.2
}

Test 'lava: the lava permutation runs, and the picture is the single shader''s' {
    SendOk 'set_shader TestRed draw_ps LavaPS.cso' | Out-Null
    Use-Lighting 'split dxc'
    $l = Get-Lighting
    Assert-True -Condition ($l.Lava -ge 1) -Message 'a frame with lava has the lava permutation'
    Assert-Equal -Expected 0 -Actual $l.Water -Message 'and no water one'
    Assert-SamePicture 'lava' -MinLit 0.2
}

Test 'back to an ordinary material: the water and lava permutations stop' {
    SendOk 'set_shader TestRed draw_ps MainRenderPS.cso', "set_component box_a Base ""{'draw_depth':true}""" | Out-Null
    Use-Lighting 'split dxc'
    $l = Get-Lighting
    Assert-Equal -Expected 0 -Actual $l.Water -Message 'no water permutation without water'
    Assert-Equal -Expected 0 -Actual $l.Lava -Message 'no lava permutation without lava'
}

Test 'lights that trace their shadow: the ray tracing permutation runs, and the picture is the single shader''s' {
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""",
        "set_component tl_1 PointLight ""{'raytraced_shadow':true}""",
        "set_component sun DirectionalLight ""{'raytraced_shadow':true}""" | Out-Null
    Use-Lighting 'split dxc'
    Step-EditorFrames -Session $Session -Count 30
    $l = Get-Lighting
    Assert-True -Condition ($l.Rt -ge 1) -Message 'a frame with lights that trace has the ray tracing permutation'
    Assert-True -Condition ($l.Standard -ge 1) -Message 'next to the standard one, which lights the rest'
    Assert-SamePicture 'traced'
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':false}""",
        "set_component tl_1 PointLight ""{'raytraced_shadow':false}""",
        "set_component sun DirectionalLight ""{'raytraced_shadow':false,'cast_shadow':true}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    Assert-Equal -Expected 0 -Actual (Get-Lighting).Rt -Message 'no light traces, no ray tracing permutation'
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}
