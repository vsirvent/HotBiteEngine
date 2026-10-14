# fixture: models
# description: Ray traced light shadows solved on the screen - a skinned mesh (the troll) is in the depth buffer and not in the BVH of casters, so only the screen can make it cast; what the BVH holds still casts, and the setting round-trips.

# A troll standing on the ground slab, one lamp to one side and above, no sun. The same lamp is
# traced three ways - in the world only (the BVH holds no skinned mesh, so the troll casts nothing),
# on the screen only, and both - and the pictures are compared in two places: where the troll's
# shadow falls on the ground, and on the open ground far from it, which no solver may change.
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on
# the viewport).

# Mean luminance of a region of the frame, as fractions of the image.
function Get-Region {
    param([string]$Path, [double]$Left, [double]$Top, [double]$Right, [double]$Bottom)
    return (Get-ImageStats -Path $Path -Left $Left -Top $Top -Right $Right -Bottom $Bottom -Step 2).Mean
}

# One picture of the scene with the lamp solved the way `$Space` says, as two measures: the ground
# in the lee of the troll (where its shadow falls) and open ground far from it.
function Measure-Shadow {
    param([string]$Space)
    SendOk "render shadow_space $Space" | Out-Null
    Step-EditorFrames -Session $Session -Count 40
    $path = Shot "shadow-$Space"
    return [pscustomobject]@{
        Path = $path
        Lee  = Get-Region $path 0.41 0.50 0.47 0.56
        Open = Get-Region $path 0.60 0.62 0.76 0.80
        Body = Get-Region $path 0.46 0.30 0.54 0.50
    }
}

Test 'set up: a troll on the ground, one ray traced lamp beside it, no sun' {
    SendOk 'place tf_troll origin' | Out-Null
    SendOk "set_component sun DirectionalLight ""{'intensity':0}""",
        "set_component ambient AmbientLight ""{'color_down':{'x':0,'y':0,'z':0},'color_up':{'x':0,'y':0,'z':0}}""" | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk 'light_gizmos off', 'light_positions off' | Out-Null
    SendOk 'spawn_lights 1 60 8 0 9' | Out-Null
    SendOk 'select tl_0', 'set_position 7 7 -2' | Out-Null
    SendOk "set_component tl_0 PointLight ""{'raytraced_shadow':true}""" | Out-Null
    SendOk 'render rt_indirect 0', 'render rt_reflections 0', 'render rt_refractions 0', 'render aa 0', 'render motion_blur 0' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 24 -60', 'camera_target 0 2 0' | Out-Null
}

Test 'in the world only the troll casts nothing: it is not in the BVH' {
    $script:world = Measure-Shadow 'world'
    # The ground beside the troll is as lit as the open ground (the lamp is the same distance
    # and the same height over both, within the falloff of a 60 unit light): no shadow.
    Assert-True -Condition ($script:world.Lee -gt $script:world.Open) `
        -Message ("the ground by the troll is lit ({0:N1} against {1:N1} far away)" -f $script:world.Lee, $script:world.Open)
}

Test 'on the screen the troll casts its shadow' {
    $script:screen = Measure-Shadow 'screen'
    Assert-True -Condition ($script:screen.Lee -lt $script:world.Lee * 0.7) `
        -Message ("the ground in the lee of the troll is darker with the screen ({0:N1}) than without it ({1:N1})" -f $script:screen.Lee, $script:world.Lee)
    # The troll's own far side is dark as well: it sits behind its own body from the lamp.
    Assert-True -Condition ($script:screen.Body -lt $script:world.Body) `
        -Message ("the troll shadows itself ({0:N1} against {1:N1})" -f $script:screen.Body, $script:world.Body)
}

Test 'the screen shadow stays where the caster is: open ground is not touched' {
    # A solver that darkened the ground at random (a surface shadowing itself) would change the
    # open ground too.
    Assert-Near -Expected $script:world.Open -Actual $script:screen.Open -Tolerance 2.0 `
        -Message 'the open ground reads the same with and without the screen'
}

Test 'both: the troll casts, and so does what the BVH holds' {
    $script:both = Measure-Shadow 'both'
    Assert-True -Condition ($script:both.Lee -lt $script:world.Lee * 0.7) `
        -Message ("the ground in the lee of the troll is dark with both ({0:N1}, {1:N1} in the world)" -f $script:both.Lee, $script:world.Lee)
    Assert-Near -Expected $script:world.Open -Actual $script:both.Open -Tolerance 2.0 -Message 'the open ground is untouched'
    # The fixture's boxes are in the BVH and not in the depth buffer's way: with the world on they
    # cast as they did, so "both" is no lighter than "screen" alone anywhere it was dark.
    Assert-True -Condition ($script:both.Lee -le $script:screen.Lee + 3.0) `
        -Message ("both is as dark as the screen alone in the lee ({0:N1} against {1:N1})" -f $script:both.Lee, $script:screen.Lee)
}

Test 'back to the world solver the level was authored with' {
    SendOk 'render shadow_space world' | Out-Null
    $r = (SendOk 'render')[0].Text | ConvertFrom-Json
    Assert-Equal -Expected 'world' -Actual $r.shadow_space
}
