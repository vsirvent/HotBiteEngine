# fixture: empty
# description: The reflections, the refractions and the indirect light traced by the hardware - an inline ray query against the acceleration structure of the objects it is handed, their textures read from an unbounded array, no limit of a hundred objects - is the default, builds what it traces, and draws what the geometry says.

# What is checked, and how:
#
# * WHO TRACED comes off the engine: `rt_tracer` answers with the tracer asked for, whether the device has ray queries
#   (`hw_supported`), whether the last frame that traced used them (`hw_active`) and what the structure holds (instances: one a
#   traced object, the same objects `rt_info` says were handed to the tracers).
# * WHAT THEY DRAW is checked against the scene, never against the software walk: that one is the older code and has bugs of its own
#   (the world reflection solver of suite 48 draws black, with either tracer, once the denoiser has had it), so a picture that
#   agrees with it proves nothing. The raw buffers are read (`rt_denoise 0`, `gi_denoise 0`): what the rays gathered, before the
#   denoisers. A mirror floor shows the boxes standing on it, mirrored, and the boxes show the floor; a box that moves takes its image
#   with it; the indirect light is gathered from a lamp where it shines and from nowhere else.
# * MORE THAN A HUNDRED OBJECTS: the software walk reads a constant buffer of MAX_OBJECTS = 100, the hardware path structured buffers
#   and the bindless table (MAX_RT_OBJECTS = 1024), so past a hundred drawables it traces them all and the software walk the best hundred.
#
# A device without ray queries skips what needs them.
#
# Only x 0.42-0.78 of the window is scene (the panels cover the rest, see TestFramework's note on the viewport).

$invariant = [Globalization.CultureInfo]::InvariantCulture

function Get-Tracer {
    $r = (SendOk 'rt_tracer')[0].Text
    $o = @{}
    foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $o[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]@{
        Tracer = $o['tracer']; Supported = ($o['hw_supported'] -eq '1'); Active = ($o['hw_active'] -eq '1')
        Instances = [int]$o['instances']; Skipped = [int]$o['skipped']; Geometries = [int]$o['geometries']; Built = [int]$o['built']
        GeometryMb = [double]::Parse($o['geometry_mb'], $invariant); TopMb = [double]::Parse($o['top_mb'], $invariant)
    }
}

function Get-TracedObjects {
    return [int]((SendOk 'rt_info')[0].Text -replace '.*objects=(\d+).*', '$1')
}

# Asks for a tracer and lets the frames that use it (and its pipelines, and the temporal history) go by.
function Use-Tracer {
    param([string]$Tracer)
    SendOk "rt_tracer $Tracer" | Out-Null
    Step-EditorFrames -Session $Session -Count 30
}

# The reflection buffer as the tracer wrote it (no denoiser), through this window of the viewport.
function Get-Reflection {
    param([string]$Name, [hashtable]$Window)
    Step-EditorFrames -Session $Session -Count 25
    return Get-ImageStats -Path (Shot $Name) @Window -Step 2
}

# Where the scene's pictures are looked at (fractions of the window): the front of the box on the right (what the floor looks like in it),
# the floor under it, where its mirror image is, and the floor beside that, where nothing is to be mirrored.
$BoxFront = @{ Left = 0.58; Top = 0.40; Right = 0.64; Bottom = 0.52 }
$Mirror = @{ Left = 0.58; Top = 0.56; Right = 0.64; Bottom = 0.69 }
$Beside = @{ Left = 0.69; Top = 0.56; Right = 0.76; Bottom = 0.69 }
# The floor beside the box on the left, which is made a lamp for the indirect light.
$NearLamp = @{ Left = 0.45; Top = 0.50; Right = 0.55; Bottom = 0.70 }

Test 'rt_tracer answers, and takes a tracer by name' {
    $t = Get-Tracer
    Assert-True -Condition ($t.Tracer -eq 'hw' -or $t.Tracer -eq 'sw') -Message "a tracer ($($t.Tracer))"
    foreach ($name in 'sw', 'software', 'hw', 'hardware') {
        Assert-Ok -Result (Send "rt_tracer $name")[0]
    }
    Assert-Err -Result (Send 'rt_tracer somewhere')[0] -Pattern 'hw\|sw'
    SendOk 'rt_tracer hw' | Out-Null
    # The hardware is what the engine starts with (a device without ray queries has the software walk whatever is asked).
    Assert-Equal -Expected 'hw' -Actual (Get-Tracer).Tracer -Message 'the hardware is the one asked for'
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
    # The floor is a perfect mirror and blue, the boxes are not: every ray off the floor is the mirror direction, so what it finds is a
    # matter of geometry.
    SendOk 'set_material box_a TestBlue', 'set_material_prop TestBlue specular 1.0',
        'set_material_prop TestBlue rt_reflex 1' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 14 -22', 'camera_target 0 2 6' | Out-Null
    SendOk 'render rt_quality high', 'render rt_reflections 1', 'render rt_refractions 1', 'render rt_indirect 1',
        'render rt_space world', 'render aa 0', 'render motion_blur 0', 'select' | Out-Null
    Step-EditorFrames -Session $Session -Count 40
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'the scene is up'
}

Test 'the hardware traces by default, over the objects the tracers were handed' {
    $t = Get-Tracer
    if (-not $t.Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    Use-Tracer 'hw'
    $t = Get-Tracer
    Assert-True -Condition $t.Active -Message 'the last frame that traced used the hardware'
    $objects = Get-TracedObjects
    Assert-True -Condition ($objects -ge 3) -Message "the floor and the boxes are traced ($objects objects)"
    Assert-Equal -Expected $objects -Actual $t.Instances -Message 'the structure holds each of them'
    Assert-Equal -Expected 0 -Actual $t.Skipped -Message 'and none was left out'
    Assert-True -Condition ($t.Geometries -ge 1) -Message "their triangles are bottom levels ($($t.Geometries))"
    Assert-True -Condition ($t.TopMb -gt 0) -Message 'and the top level exists'
}

Test 'the software walk traces when it is asked to, and the hardware is back when it is asked again' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    Use-Tracer 'sw'
    Assert-False -Condition (Get-Tracer).Active -Message 'the last frame that traced used the software walk'
    Use-Tracer 'hw'
    Assert-True -Condition (Get-Tracer).Active -Message 'and the hardware is back with the next'
}

Test 'the boxes show the floor, and the floor shows the boxes' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    Use-Tracer 'hw'
    SendOk 'render debug_buffer reflection', 'render debug_gain 4', 'render rt_denoise 0' | Out-Null
    $front = Get-Reflection 'box-front' $BoxFront
    $mirror = Get-Reflection 'mirror' $Mirror
    $beside = Get-Reflection 'beside' $Beside
    SendOk 'render debug_buffer off', 'render rt_denoise 1' | Out-Null
    # A box is not a mirror (it spreads its rays), but what its rays find, looking down at the floor, is the blue of the floor.
    Assert-True -Condition ($front.LitShare -gt 0.3) -Message ("the front of the box reflects ({0:P0} of its pixels are lit)" -f $front.LitShare)
    Assert-True -Condition ($front.MeanB -gt 2.0 * [Math]::Max($front.MeanR, $front.MeanG)) `
        -Message ("and what it reflects is blue: the floor (R {0:N0} G {1:N0} B {2:N0})" -f $front.MeanR, $front.MeanG, $front.MeanB)
    # The floor in front of the box mirrors it: where the ray from the camera bounces off the floor and up into the box.
    Assert-True -Condition ($mirror.LitShare -gt 0.3) -Message ("the floor under the box shows its image ({0:P0} of the pixels are lit)" -f $mirror.LitShare)
    # And the floor beside it has nothing to mirror: the rays go up into the sky.
    Assert-True -Condition ($beside.Mean -lt $mirror.Mean * 0.2) `
        -Message ("which is not there beside it (mean {0:N1} against {1:N1})" -f $beside.Mean, $mirror.Mean)
}

Test 'a box that moves is in its new place for the next frame''s rays' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    Use-Tracer 'hw'
    SendOk 'render debug_buffer reflection', 'render debug_gain 4', 'render rt_denoise 0' | Out-Null
    $before = Get-Reflection 'moved-before' $Mirror
    SendOk 'select box_c', 'set_position 26 3 10', 'select' | Out-Null
    $after = Get-Reflection 'moved-after' $Mirror
    SendOk 'select box_c', 'set_position 6 3 10', 'select' | Out-Null
    Step-EditorFrames -Session $Session -Count 20
    SendOk 'render debug_buffer off', 'render rt_denoise 1' | Out-Null
    Assert-True -Condition ($before.LitShare -gt 0.3) -Message ("the image is there ({0:P0} lit)" -f $before.LitShare)
    Assert-True -Condition ($after.Mean -lt $before.Mean * 0.2) -Message ("and leaves with the box (mean {0:N1} after, {1:N1} before)" -f $after.Mean, $before.Mean)
    Assert-True -Condition (Get-Tracer).Active -Message 'the hardware traced it'
}

Test 'the indirect light is gathered from a lamp, where it shines' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    # A box that gives off light (the lamp is `emission`, which a ray reads from the object's material when it hits it): the floor and the
    # box beside it are lit by it and by nothing else the rays find - the rest of the scene is open sky. Which object a hit is picks the
    # material, so a lamp that lights the wrong thing, or nothing, is a hit read from the wrong place.
    SendOk 'create_material TestLamp materials\test.mat', 'set_material box_b TestLamp', 'set_material_prop TestLamp emission 8' | Out-Null
    Use-Tracer 'hw'
    # The raw buffer of the pass, what its rays gathered, before the denoiser takes the noise out.
    SendOk 'render debug_buffer indirect', 'render gi_denoise 0', 'render debug_gain 8' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    $near = Get-ImageStats -Path (Shot 'lamp-near') @NearLamp -Step 2
    SendOk 'select box_b', 'set_position -44 4 6', 'select' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    $far = Get-ImageStats -Path (Shot 'lamp-far') @NearLamp -Step 2
    SendOk 'select box_b', 'set_position -4 4 6', 'select', 'set_material box_b __default_material',
        'render debug_buffer off', 'render gi_denoise 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 20
    Assert-True -Condition ($near.LitShare -ge 0.3) -Message ("the lamp lights the floor beside it: {0:P0} of the pixels are lit" -f $near.LitShare)
    Assert-True -Condition ($far.Mean -lt $near.Mean * 0.1) `
        -Message ("and it is the lamp that does: {0:N1} with it beside the floor, {1:N1} with it away" -f $near.Mean, $far.Mean)
}

Test 'past a hundred objects the hardware traces them all and the software walk the best hundred' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    # Copies of a box, in a grid: each is a drawable the tracers are handed.
    SendOk 'select box_c', 'copy' | Out-Null
    $made = 0
    for ($batch = 0; $batch -lt 6; $batch++) {
        $commands = @()
        for ($i = 0; $i -lt 25; $i++) {
            $n = $batch * 25 + $i
            $commands += 'paste'
            $commands += ('set_position {0} 1 {1}' -f (($n % 15) * 3 - 20), ([int][Math]::Floor($n / 15) * 3 + 14))
            $commands += 'set_scale 1 2 1'
        }
        SendOk $commands | Out-Null
        $made += 25
    }
    Use-Tracer 'hw'
    $hw = Get-TracedObjects
    $t = Get-Tracer
    Assert-True -Condition ($hw -gt 140) -Message "the hardware is handed all of them ($hw objects after $made copies)"
    Assert-Equal -Expected $hw -Actual $t.Instances -Message 'and its structure holds each'
    Use-Tracer 'sw'
    $sw = Get-TracedObjects
    Assert-Equal -Expected 100 -Actual $sw -Message 'the software walk holds a hundred of them'
    Use-Tracer 'hw'
    Assert-Equal -Expected $hw -Actual (Get-TracedObjects) -Message 'and the hardware has them all again'
    Assert-True -Condition (Get-Tracer).Active -Message 'tracing'
}

Test 'switching tracers back and forth is harmless' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    foreach ($i in 1..4) {
        Use-Tracer 'sw'
        Use-Tracer 'hw'
    }
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
    Assert-True -Condition (Get-Tracer).Active -Message 'and the hardware traces'
}

Test 'with the reflections and the indirect light off, there is nothing to build' {
    if (-not (Get-Tracer).Supported) { Skip-Test -Reason 'no ray queries on this device'; return }
    SendOk 'render rt_reflections 0', 'render rt_refractions 0', 'render rt_indirect 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
    SendOk 'render rt_reflections 1', 'render rt_refractions 1', 'render rt_indirect 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 30
    Assert-True -Condition (Get-Tracer).Active -Message 'and they trace again'
}
