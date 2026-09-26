# fixture: empty
# description: Platform, LinearPlatform and Force - the motion each drives (with and without a rigid body), the pause/rewind contract, and the viewport gizmos.

# These three components are authoring-only in the editor: World::Run drives their
# systems from the physics tick and skips them while physics is paused, which the Scene
# Editor keeps it. So every motion test here is the same shape - author, read the pose,
# switch Edit/Simulate Physics on, step frames, read it again - and "it did not move
# while paused" is as much a requirement as "it moved when simulating".
#
# The gizmo half is read back through `motion_gizmo_info` rather than from pixels,
# because a platform at rest looks exactly like a static prop and the counters are the
# only thing that can tell them apart.

function Simulate {
    param([int]$Frames = 30)
    SendOk 'menu "Edit/Simulate Physics"' | Out-Null
    Step-EditorFrames -Session $Session -Count $Frames
}

function StopSimulating {
    SendOk 'menu "Edit/Simulate Physics"' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
}

# The pose of `Entity` over a run of simulated frames, as the min/max it reached on each
# axis. One capture is not enough for an oscillation: a sine read at one instant can be
# anywhere, including back at the centre.
function Measure-Travel {
    param([string]$Entity, [int]$Frames = 60)
    $xs = @(); $ys = @(); $zs = @()
    for ($i = 0; $i -lt $Frames; $i++) {
        $p = Get-Position -Session $Session -Entity $Entity
        $xs += [double]$p.x; $ys += [double]$p.y; $zs += [double]$p.z
    }
    return [pscustomobject]@{
        MinX = ($xs | Measure-Object -Minimum).Minimum; MaxX = ($xs | Measure-Object -Maximum).Maximum
        MinY = ($ys | Measure-Object -Minimum).Minimum; MaxY = ($ys | Measure-Object -Maximum).Maximum
        MinZ = ($zs | Measure-Object -Minimum).Minimum; MaxZ = ($zs | Measure-Object -Maximum).Maximum
    }
}

function Get-GizmoInfo {
    Step-EditorFrames -Session $Session -Count 3
    $r = (SendOk 'motion_gizmo_info')[0]
    $m = [regex]::Match($r.Text, 'platforms=(\d+) linears=(\d+) forces=(\d+) segments=(\d+) markers=(\d+)')
    if (-not $m.Success) { throw "unparseable motion_gizmo_info: $($r.Text)" }
    $markers = @{}
    foreach ($line in $r.Payload) {
        $p = $line -split ' '
        if ($p.Count -ge 5) {
            $markers[$p[0]] = [pscustomobject]@{ Kind = $p[1]; Detail = $p[2]; X = [double]$p[3]; Y = [double]$p[4] }
        }
    }
    return [pscustomobject]@{
        Platforms = [int]$m.Groups[1].Value; Linears = [int]$m.Groups[2].Value
        Forces = [int]$m.Groups[3].Value; Segments = [int]$m.Groups[4].Value
        MarkerCount = [int]$m.Groups[5].Value; Markers = $markers
    }
}

# ---- registration ---------------------------------------------------------------------

Test 'the three components are registered, addable and inert at their defaults' {
    foreach ($component in @('Platform', 'LinearPlatform', 'Force')) {
        SendOk "add_component box_a $component" | Out-Null
        $components = (SendOk 'components box_a')[0].Payload
        Assert-Contains -Collection $components -Value $component `
            -Message "$component can be added to an ordinary entity"
    }
    # Nothing moves and nothing pushes until values are typed: adding one of these must
    # never disturb a scene.
    $platform = Get-Component -Session $Session -Entity 'box_a' -Component 'Platform'
    Assert-Near -Expected 0 -Actual $platform.amplitude -Tolerance 0.0001
    Assert-Near -Expected 0 -Actual $platform.angular_speed -Tolerance 0.0001
    Assert-Near -Expected -1 -Actual $platform.fall_delay -Tolerance 0.0001 `
        -Message 'fall_delay defaults to never'
    $linear = Get-Component -Session $Session -Entity 'box_a' -Component 'LinearPlatform'
    Assert-Near -Expected 0 -Actual $linear.speed -Tolerance 0.0001
    Assert-Equal -Expected 'True' -Actual $linear.ping_pong
    $force = Get-Component -Session $Session -Entity 'box_a' -Component 'Force'
    Assert-Equal -Expected 'NONE' -Actual $force.type
    Assert-Near -Expected 0 -Actual $force.force -Tolerance 0.0001

    foreach ($component in @('Platform', 'LinearPlatform', 'Force')) {
        SendOk "remove_component box_a $component" | Out-Null
    }
}

Test 'NonPhysicPlatform is gone: a Platform with no Physics component is what replaced it' {
    # The old engine/Marbles split had a second component for the no-rigid-body case.
    # Adding it must now fail rather than silently do nothing.
    Assert-Err -Result (Send 'add_component box_a NonPhysicPlatform')[0] -Pattern '.'
}

Test 'every authored field round-trips' {
    SendOk 'add_component box_a Platform' | Out-Null
    SendOk "set_component box_a Platform ""{'linear_dir':{'x':1,'y':0,'z':0},'amplitude':2.5,'freq':0.75,'phase':0.25,'angular_dir':{'x':0,'y':0,'z':1},'angular_speed':1.5,'delay':0.5,'fall_delay':4}""" | Out-Null
    $p = Get-Component -Session $Session -Entity 'box_a' -Component 'Platform'
    Assert-Vector3Near -Expected @{ x = 1.0; y = 0.0; z = 0.0 } -Actual $p.linear_dir -Tolerance 0.0001
    Assert-Near -Expected 2.5 -Actual $p.amplitude -Tolerance 0.0001
    Assert-Near -Expected 0.75 -Actual $p.freq -Tolerance 0.0001
    Assert-Near -Expected 0.25 -Actual $p.phase -Tolerance 0.0001
    Assert-Vector3Near -Expected @{ x = 0.0; y = 0.0; z = 1.0 } -Actual $p.angular_dir -Tolerance 0.0001
    Assert-Near -Expected 1.5 -Actual $p.angular_speed -Tolerance 0.0001
    Assert-Near -Expected 0.5 -Actual $p.delay -Tolerance 0.0001
    Assert-Near -Expected 4 -Actual $p.fall_delay -Tolerance 0.0001

    SendOk 'add_component box_a LinearPlatform' | Out-Null
    SendOk "set_component box_a LinearPlatform ""{'travel':{'x':0,'y':3,'z':0},'speed':2,'delay':0.25,'ping_pong':false}""" | Out-Null
    $l = Get-Component -Session $Session -Entity 'box_a' -Component 'LinearPlatform'
    Assert-Vector3Near -Expected @{ x = 0.0; y = 3.0; z = 0.0 } -Actual $l.travel -Tolerance 0.0001
    Assert-Near -Expected 2 -Actual $l.speed -Tolerance 0.0001
    Assert-Near -Expected 0.25 -Actual $l.delay -Tolerance 0.0001
    Assert-Equal -Expected 'False' -Actual $l.ping_pong

    SendOk 'add_component box_a Force' | Out-Null
    SendOk "set_component box_a Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'local_dir':true,'force':120,'origin_force':30,'range':8,'radius':2}""" | Out-Null
    $f = Get-Component -Session $Session -Entity 'box_a' -Component 'Force'
    # The class round-trips as a name, not as the integer the enum happens to be.
    Assert-Equal -Expected 'PROJECTION' -Actual $f.type
    Assert-Equal -Expected 'True' -Actual $f.local_dir
    Assert-Near -Expected 120 -Actual $f.force -Tolerance 0.0001
    Assert-Near -Expected 30 -Actual $f.origin_force -Tolerance 0.0001
    Assert-Near -Expected 8 -Actual $f.range -Tolerance 0.0001
    Assert-Near -Expected 2 -Actual $f.radius -Tolerance 0.0001

    foreach ($component in @('Platform', 'LinearPlatform', 'Force')) {
        SendOk "remove_component box_a $component" | Out-Null
    }
}

# ---- Platform: the oscillation --------------------------------------------------------

Test 'set up: box_a is a kinematic platform and box_b a bodiless one' {
    # A kinematic body, so PlatformSystem moves the body and PhysicsSystem writes the
    # pose back into the Transform.
    SendOk 'select box_a', "set_component box_a Physics ""{'type':'KINEMATIC','shape':'BOX'}""",
        'set_position 0 5 0' | Out-Null
    # And one with no rigid body at all - the case the old NonPhysicPlatform existed for.
    # PlatformSystem writes its Transform directly and StaticMeshSystem recomposes it.
    SendOk 'select box_b', 'remove_component box_b Physics', 'set_position 8 5 0' | Out-Null
    SendOk 'camera_pos 0 12 -26' | Out-Null
    Step-EditorFrames -Session $Session -Count 5
}

Test 'a platform does not move while physics is paused' {
    SendOk 'add_component box_a Platform' | Out-Null
    SendOk "set_component box_a Platform ""{'linear_dir':{'x':0,'y':1,'z':0},'amplitude':3,'freq':1}""" | Out-Null
    $before = Get-Position -Session $Session -Entity 'box_a'
    Step-EditorFrames -Session $Session -Count 40
    Assert-Vector3Near -Expected $before -Actual (Get-Position -Session $Session -Entity 'box_a') `
        -Tolerance 0.001 -Message 'the editor authors a scene; a platform sits still until asked to simulate'
}

Test 'simulating moves the platform along linear_dir, and only along it' {
    $authored = Get-Position -Session $Session -Entity 'box_a'
    Simulate -Frames 5
    $travel = Measure-Travel -Entity 'box_a' -Frames 70
    StopSimulating

    Assert-True -Condition (($travel.MaxY - $travel.MinY) -gt 1.0) `
        -Message "the platform travelled on Y (min $($travel.MinY), max $($travel.MaxY))"
    # The amplitude is authored in world units, so the reach is a number to check rather
    # than a direction to eyeball: 3 either side of the centre and no further.
    Assert-True -Condition ($travel.MaxY -le ($authored.y + 3.0 + 0.05)) `
        -Message "it never goes past +amplitude (max $($travel.MaxY), limit $($authored.y + 3.0))"
    Assert-True -Condition ($travel.MinY -ge ($authored.y - 3.0 - 0.05)) `
        -Message "nor past -amplitude (min $($travel.MinY), limit $($authored.y - 3.0))"
    Assert-True -Condition (($travel.MaxX - $travel.MinX) -lt 0.01 -and ($travel.MaxZ - $travel.MinZ) -lt 0.01) `
        -Message 'nothing moved on the axes it was not given'
}

Test 'switching the preview off rewinds the platform to its authored pose' {
    # PhysicsPreview::IsSimulated has to count a Platform as simulated, not only a
    # non-static body - otherwise a platform is left wherever the preview stopped it and
    # the level quietly drifts every time somebody previews it.
    SendOk 'select box_a', 'set_position 0 5 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $authored = Get-Position -Session $Session -Entity 'box_a'
    Simulate -Frames 40
    $moved = Get-Position -Session $Session -Entity 'box_a'
    Assert-True -Condition ([math]::Abs($moved.y - $authored.y) -gt 0.1) `
        -Message "the platform really did leave its authored pose (authored $($authored.y), moved $($moved.y))"
    StopSimulating
    Assert-Vector3Near -Expected $authored -Actual (Get-Position -Session $Session -Entity 'box_a') `
        -Tolerance 0.05 -Message 'and is put back where it was authored'
}

Test 'a platform with no rigid body moves too - that is what NonPhysicPlatform was for' {
    SendOk 'add_component box_b Platform' | Out-Null
    SendOk "set_component box_b Platform ""{'linear_dir':{'x':1,'y':0,'z':0},'amplitude':2,'freq':1}""" | Out-Null
    $components = (SendOk 'components box_b')[0].Payload
    Assert-NotContains -Collection $components -Value 'Physics' -Message 'no rigid body on this one'
    Simulate -Frames 5
    $travel = Measure-Travel -Entity 'box_b' -Frames 70
    StopSimulating
    Assert-True -Condition (($travel.MaxX - $travel.MinX) -gt 0.7) `
        -Message "it travelled on X through its Transform alone (min $($travel.MinX), max $($travel.MaxX))"
}

Test 'delay holds the platform still, then it moves' {
    SendOk "set_component box_b Platform ""{'linear_dir':{'x':1,'y':0,'z':0},'amplitude':2,'freq':2,'delay':2.5}""" | Out-Null
    $authored = Get-Position -Session $Session -Entity 'box_b'
    Simulate -Frames 10
    $early = Get-Position -Session $Session -Entity 'box_b'
    Assert-Near -Expected $authored.x -Actual $early.x -Tolerance 0.05 `
        -Message "still waiting out the delay (authored $($authored.x), now $($early.x))"
    # The systems' clocks advance only on the ticks they run, so the wait is in frames of
    # simulation rather than in wall time.
    $travel = Measure-Travel -Entity 'box_b' -Frames 400
    StopSimulating
    Assert-True -Condition (($travel.MaxX - $travel.MinX) -gt 0.5) `
        -Message "and then it moves (min $($travel.MinX), max $($travel.MaxX))"
    SendOk "set_component box_b Platform ""{'delay':0}""" | Out-Null
}

Test 'angular_speed spins the platform, on top of the authored rotation' {
    SendOk 'select box_b' | Out-Null
    SendOk "set_component box_b Platform ""{'amplitude':0,'angular_dir':{'x':0,'y':1,'z':0},'angular_speed':2}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $before = (Get-Component -Session $Session -Entity 'box_b' -Component 'Transform').rotation
    Simulate -Frames 20
    $during = (Get-Component -Session $Session -Entity 'box_b' -Component 'Transform').rotation
    StopSimulating
    $delta = [math]::Abs($during.y - $before.y) + [math]::Abs($during.w - $before.w)
    Assert-True -Condition ($delta -gt 0.01) `
        -Message "the rotation quaternion turned (before y=$($before.y) w=$($before.w), during y=$($during.y) w=$($during.w))"
}

Test 'a spin starts from the rotation the platform was authored with, not its import one' {
    # Transform::initial_rotation is written by FBXLoader and by nothing else - not by
    # Transform::FromJson, not by the gizmo - so a spin composed onto it snaps a rotated
    # platform back to its import rotation the instant it starts turning. The spin's zero
    # is latched from the live rotation instead.
    SendOk 'select box_b' | Out-Null
    SendOk 'set_rotation 0 90 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 4
    $authored = (Get-Component -Session $Session -Entity 'box_b' -Component 'Transform').rotation
    Assert-True -Condition ([math]::Abs($authored.y) -gt 0.5) `
        -Message "the platform really is turned 90 degrees (y=$($authored.y))"
    SendOk "set_component box_b Platform ""{'amplitude':0,'angular_dir':{'x':0,'y':1,'z':0},'angular_speed':0.2}""" | Out-Null
    Simulate -Frames 4
    $spinning = (Get-Component -Session $Session -Entity 'box_b' -Component 'Transform').rotation
    StopSimulating
    # A few frames of a slow spin: still essentially the authored rotation. Composed onto
    # the identity initial_rotation it would have jumped most of the way back to zero.
    Assert-Near -Expected $authored.y -Actual $spinning.y -Tolerance 0.1 `
        -Message "the spin began from the authored rotation (authored y=$($authored.y), spinning y=$($spinning.y))"
    SendOk 'select box_b', 'set_rotation 0 0 0' | Out-Null
    SendOk "set_component box_b Platform ""{'angular_speed':0}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
}

Test 'a Platform on a STATIC body is promoted to KINEMATIC, or it would move invisibly' {
    # PhysicsSystem::Update writes a Transform back only for a body that is not STATIC,
    # so a STATIC platform slides through the collision world while standing still on
    # screen. PlatformSystem promotes it on its first tick.
    SendOk 'select box_c', "set_component box_c Physics ""{'type':'STATIC','shape':'BOX'}""",
        'set_position -8 5 0' | Out-Null
    SendOk 'add_component box_c Platform' | Out-Null
    SendOk "set_component box_c Platform ""{'linear_dir':{'x':0,'y':1,'z':0},'amplitude':2,'freq':1}""" | Out-Null
    Assert-Equal -Expected 'STATIC' `
        -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'Physics').type `
        -Message 'still static while nothing has ticked'
    Simulate -Frames 10
    Assert-Equal -Expected 'KINEMATIC' `
        -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'Physics').type `
        -Message 'promoted once the platform started moving'
    $travel = Measure-Travel -Entity 'box_c' -Frames 60
    StopSimulating
    Assert-True -Condition (($travel.MaxY - $travel.MinY) -gt 0.7) `
        -Message "and the promotion is what makes the motion visible (min $($travel.MinY), max $($travel.MaxY))"
    SendOk 'remove_component box_c Platform' | Out-Null
}

Test 'an inert platform leaves its body type and its static flag alone' {
    # The promotion happens on the first move, not the first tick: a Platform at its
    # defaults is going nowhere, and neither a body type nor a caster's shadow path should
    # change for a piece of scenery because somebody added an empty component to it.
    SendOk 'select box_c', "set_component box_c Physics ""{'type':'STATIC','shape':'BOX'}""",
        "set_component box_c Base ""{'is_static':true}""" | Out-Null
    SendOk 'add_component box_c Platform' | Out-Null
    Simulate -Frames 25
    StopSimulating
    Assert-Equal -Expected 'STATIC' `
        -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'Physics').type `
        -Message 'an inert platform is not a reason to make a static body kinematic'
    Assert-Equal -Expected 'True' `
        -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'Base').is_static `
        -Message 'nor to take its caster out of the static shadow map'
    SendOk 'remove_component box_c Platform' | Out-Null
    SendOk "set_component box_c Base ""{'is_static':false}""" | Out-Null
}

# ---- LinearPlatform -------------------------------------------------------------------

Test 'a LinearPlatform walks its travel and reverses at the end' {
    SendOk 'select box_b', 'remove_component box_b Platform', 'set_position 8 5 0' | Out-Null
    SendOk 'add_component box_b LinearPlatform' | Out-Null
    SendOk "set_component box_b LinearPlatform ""{'travel':{'x':0,'y':4,'z':0},'speed':6,'ping_pong':true}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $authored = Get-Position -Session $Session -Entity 'box_b'
    Simulate -Frames 5
    $travel = Measure-Travel -Entity 'box_b' -Frames 120
    StopSimulating
    # The path is an offset from where it was authored, so both ends are known exactly -
    # and `t` is a fraction of the path, so a long tick cannot carry it past either one.
    Assert-True -Condition ($travel.MaxY -le ($authored.y + 4.0 + 0.05)) `
        -Message "it stops at the far end (max $($travel.MaxY), end $($authored.y + 4.0))"
    Assert-True -Condition ($travel.MinY -ge ($authored.y - 0.05)) `
        -Message "and never goes behind the start (min $($travel.MinY), start $($authored.y))"
    Assert-True -Condition (($travel.MaxY - $travel.MinY) -gt 2.0) `
        -Message "it covered most of the path in both directions (min $($travel.MinY), max $($travel.MaxY))"
}

Test 'ping_pong off stops the platform at the far end instead of bringing it back' {
    SendOk 'select box_b', 'set_position 8 5 0' | Out-Null
    SendOk "set_component box_b LinearPlatform ""{'travel':{'x':0,'y':4,'z':0},'speed':6,'ping_pong':false}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $authored = Get-Position -Session $Session -Entity 'box_b'
    Simulate -Frames 90
    $settled = Get-Position -Session $Session -Entity 'box_b'
    $after = Measure-Travel -Entity 'box_b' -Frames 40
    StopSimulating
    Assert-Near -Expected ($authored.y + 4.0) -Actual $settled.y -Tolerance 0.1 `
        -Message "it arrived at the far end (expected $($authored.y + 4.0), got $($settled.y))"
    Assert-True -Condition (($after.MaxY - $after.MinY) -lt 0.01) `
        -Message "and stayed there (min $($after.MinY), max $($after.MaxY))"
}

# ---- Force ----------------------------------------------------------------------------

# A force field only acts on DYNAMIC bodies, so the measurement is how far a falling box
# gets: inside an upward beam it falls less far than outside it. Against gravity rather
# than against an absolute, because the absolute depends on the tick rate.
function Measure-Fall {
    param([int]$Frames = 45)
    SendOk 'select faller', 'set_position 0 12 0',
        "set_component faller Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $start = Get-Position -Session $Session -Entity 'faller'
    Simulate -Frames $Frames
    $end = Get-Position -Session $Session -Entity 'faller'
    StopSimulating
    return ($start.y - $end.y)
}

Test 'set up: a dynamic box to be pushed, and a fan under it' {
    SendOk 'select', 'menu "Add/Entity"' | Out-Null
    SendOk 'rename Entity faller' | Out-Null
    SendOk 'select faller', 'set_position 0 12 0' | Out-Null
    # Bounds before Physics: World::Init sizes a collider from the entity's local box and
    # requires both components to exist.
    SendOk 'add_component faller Bounds', 'add_component faller Mesh',
        'add_component faller Physics' | Out-Null
    SendOk "set_component faller Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null

    SendOk 'select', 'menu "Add/Entity"' | Out-Null
    SendOk 'rename Entity fan' | Out-Null
    SendOk 'select fan', 'set_position 0 0 0', 'add_component fan Force' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
}

Test 'a PROJECTION field slows a body falling through it' {
    SendOk "set_component fan Force ""{'type':'NONE'}""" | Out-Null
    $free = Measure-Fall
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'force':4000,'range':40,'radius':6}""" | Out-Null
    $pushed = Measure-Fall
    Assert-True -Condition ($free -gt 0.5) -Message "gravity works at all (free fall $free)"
    Assert-True -Condition ($pushed -lt ($free - 0.2)) `
        -Message "the beam held the body up (free fall $free, in the beam $pushed)"
}

Test 'a body outside the beam radius feels nothing' {
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'force':4000,'range':40,'radius':6}""" | Out-Null
    $inside = Measure-Fall
    # The field is a cylinder, not a cone: move the fan sideways past the radius and the
    # same body at the same place must be untouched.
    SendOk 'select fan', 'set_position 30 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $outside = Measure-Fall
    SendOk 'select fan', 'set_position 0 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    Assert-True -Condition ($outside -gt ($inside + 0.2)) `
        -Message "outside the radius it falls freely again (inside $inside, outside $outside)"
}

Test 'reach bounds the field: a body beyond range is outside it' {
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'force':4000,'range':40,'radius':6}""" | Out-Null
    $far = Measure-Fall
    SendOk "set_component fan Force ""{'range':2}""" | Out-Null
    $short = Measure-Fall
    Assert-True -Condition ($short -gt ($far + 0.2)) `
        -Message "a 2-unit reach does not touch a body 12 units up (reach 40: $far, reach 2: $short)"
}

Test 'a NONE field does nothing, however it is configured' {
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'force':4000,'range':40,'radius':6}""" | Out-Null
    $pushed = Measure-Fall
    SendOk "set_component fan Force ""{'type':'NONE'}""" | Out-Null
    $off = Measure-Fall
    Assert-True -Condition ($off -gt ($pushed + 0.2)) `
        -Message "switching the class to NONE stops it pushing (PROJECTION $pushed, NONE $off)"
}

# ---- gizmos ---------------------------------------------------------------------------
# Segments are counted as they are emitted, before any clipping, so the counts are exact.
# How each is composed:
#   Platform, oscillating: the travel segment (1) + an end tick at each end (2 x 2)
#                          + one chevron for the starting direction (4)          = 9
#   Platform, spinning:    the axis circle (32) + the axis itself (1)
#                          + one chevron for the sense of rotation (4)           = 37
#   Platform, inert:       one cross at the entity (2)                           = 2
#   LinearPlatform:        the path (1) + an end tick at each end (4)
#                          + a chevron at the point it has reached (4)           = 9
#   Force, PROJECTION:     a circle at each end of the cylinder (2 x 32)
#                          + 4 rails + the axis (1) + 6 beam chevrons (6 x 4)    = 93
#   Force, TOUCH:          the arrow (1) + its head (4) + a cross at the base (2) = 7
$PlatformLinear = 9
$PlatformSpin = 37
$PlatformInert = 2
$LinearSegments = 9
$ProjectionSegments = 93
$TouchSegments = 7

Test 'the overlays default to Selection, and both menu radio groups are registered' {
    $state = Get-State -Session $Session
    Assert-Equal -Expected 'selection' -Actual $state.platform_view
    Assert-Equal -Expected 'selection' -Actual $state.force_view
    $menus = (SendOk 'menus')[0].Payload -join "`n"
    foreach ($item in @('View/Platform Gizmos: Selection', 'View/Platform Gizmos: All',
            'View/Force Gizmos: Selection', 'View/Force Gizmos: All')) {
        Assert-True -Condition ($menus -match [regex]::Escape($item)) -Message "menu $item is registered"
    }
}

Test 'the switches take off/selection/all and reject anything else' {
    foreach ($cmd in @('platform_gizmos', 'force_gizmos')) {
        foreach ($mode in @('off', 'selection', 'all')) {
            $r = SendOk "$cmd $mode"
            Assert-Equal -Expected "$cmd $mode" -Actual $r[0].Text
        }
        Assert-Err -Result (Send "$cmd sideways")[0] -Pattern 'usage'
    }
    SendOk 'platform_gizmos selection', 'force_gizmos selection' | Out-Null
    $state = Get-State -Session $Session
    Assert-Equal -Expected 'selection' -Actual $state.platform_view
    Assert-Equal -Expected 'selection' -Actual $state.force_view
}

Test 'the menu entries are radio groups: the active one turns the view off' {
    SendOk 'menu "View/Platform Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'all' -Actual (Get-State -Session $Session).platform_view
    SendOk 'menu "View/Platform Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'off' -Actual (Get-State -Session $Session).platform_view
    SendOk 'menu "View/Force Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'all' -Actual (Get-State -Session $Session).force_view
    SendOk 'menu "View/Force Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'off' -Actual (Get-State -Session $Session).force_view
    SendOk 'menu "View/Platform Gizmos: Selection"', 'menu "View/Force Gizmos: Selection"' | Out-Null
}

Test 'platform gizmos: the shape follows the selection and matches the motion authored' {
    SendOk 'select box_a' | Out-Null
    SendOk "set_component box_a Platform ""{'linear_dir':{'x':0,'y':1,'z':0},'amplitude':3,'freq':1,'phase':0,'angular_speed':0}""" | Out-Null
    SendOk 'platform_gizmos selection', 'force_gizmos off', 'select' | Out-Null
    $none = Get-GizmoInfo
    Assert-Equal -Expected 0 -Actual $none.Platforms -Message 'nothing selected, nothing drawn'
    Assert-Equal -Expected 0 -Actual $none.Segments

    SendOk 'select box_a' | Out-Null
    $linear = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $linear.Platforms
    Assert-Equal -Expected $PlatformLinear -Actual $linear.Segments
    Assert-Equal -Expected 'linear' -Actual $linear.Markers['box_a'].Detail

    SendOk "set_component box_a Platform ""{'amplitude':0,'angular_dir':{'x':0,'y':1,'z':0},'angular_speed':2}""" | Out-Null
    $spin = Get-GizmoInfo
    Assert-Equal -Expected $PlatformSpin -Actual $spin.Segments -Message 'a spin draws its axis circle'
    Assert-Equal -Expected 'spin' -Actual $spin.Markers['box_a'].Detail

    SendOk "set_component box_a Platform ""{'amplitude':3}""" | Out-Null
    $both = Get-GizmoInfo
    Assert-Equal -Expected ($PlatformLinear + $PlatformSpin) -Actual $both.Segments
    Assert-Equal -Expected 'linear+spin' -Actual $both.Markers['box_a'].Detail

    SendOk "set_component box_a Platform ""{'amplitude':0,'angular_speed':0}""" | Out-Null
    $inert = Get-GizmoInfo
    Assert-Equal -Expected $PlatformInert -Actual $inert.Segments `
        -Message 'an authored but inert platform still says it is there'
    Assert-Equal -Expected 'inert' -Actual $inert.Markers['box_a'].Detail
}

Test 'platform gizmos: a LinearPlatform draws its path, and no path when it has none' {
    SendOk 'select box_b' | Out-Null
    SendOk "set_component box_b LinearPlatform ""{'travel':{'x':0,'y':4,'z':0},'speed':6,'ping_pong':true}""" | Out-Null
    $info = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $info.Linears
    Assert-Equal -Expected $LinearSegments -Actual $info.Segments
    Assert-Equal -Expected 'linear' -Actual $info.Markers['box_b'].Kind
    Assert-Equal -Expected 'ping_pong' -Actual $info.Markers['box_b'].Detail

    SendOk "set_component box_b LinearPlatform ""{'ping_pong':false}""" | Out-Null
    Assert-Equal -Expected 'one_way' -Actual (Get-GizmoInfo).Markers['box_b'].Detail

    SendOk "set_component box_b LinearPlatform ""{'travel':{'x':0,'y':0,'z':0}}""" | Out-Null
    $empty = Get-GizmoInfo
    Assert-Equal -Expected 2 -Actual $empty.Segments -Message 'no travel, so just a cross'
    Assert-Equal -Expected 'inert' -Actual $empty.Markers['box_b'].Detail
    SendOk "set_component box_b LinearPlatform ""{'travel':{'x':0,'y':4,'z':0},'ping_pong':true}""" | Out-Null
}

Test 'force gizmos: PROJECTION draws its cylinder, TOUCH an arrow' {
    SendOk 'platform_gizmos off', 'force_gizmos selection', 'select fan' | Out-Null
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'force':4000,'origin_force':0,'range':40,'radius':6}""" | Out-Null
    $projection = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $projection.Forces
    Assert-Equal -Expected $ProjectionSegments -Actual $projection.Segments
    Assert-Equal -Expected 'PROJECTION' -Actual $projection.Markers['fan'].Detail

    SendOk "set_component fan Force ""{'type':'TOUCH'}""" | Out-Null
    $touch = Get-GizmoInfo
    Assert-Equal -Expected $TouchSegments -Actual $touch.Segments `
        -Message 'TOUCH has no volume, so it is an arrow'
    Assert-Equal -Expected 'TOUCH' -Actual $touch.Markers['fan'].Detail

    # A field with no direction cannot be drawn as one, and says so rather than drawing a
    # degenerate shape (the basis for the perpendicular plane would collapse).
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':0,'z':0}}""" | Out-Null
    Assert-Equal -Expected 'no_dir' -Actual (Get-GizmoInfo).Markers['fan'].Detail
    SendOk "set_component fan Force ""{'type':'PROJECTION','dir':{'x':0,'y':1,'z':0},'radius':6,'range':40}""" | Out-Null
}

Test 'the two overlays are independent, and All draws every one of its kind' {
    SendOk 'select', 'platform_gizmos all', 'force_gizmos off' | Out-Null
    $platforms_only = Get-GizmoInfo
    Assert-Equal -Expected 0 -Actual $platforms_only.Forces `
        -Message 'force_gizmos off draws no field even while platforms are on'
    Assert-True -Condition ($platforms_only.Platforms -ge 1) -Message 'every platform is drawn'
    Assert-True -Condition ($platforms_only.Linears -ge 1) -Message 'and every linear platform'

    SendOk 'platform_gizmos off', 'force_gizmos all' | Out-Null
    $forces_only = Get-GizmoInfo
    Assert-Equal -Expected 0 -Actual $forces_only.Platforms
    Assert-Equal -Expected 0 -Actual $forces_only.Linears
    Assert-True -Condition ($forces_only.Forces -ge 1)

    SendOk 'platform_gizmos off', 'force_gizmos off' | Out-Null
    Assert-Equal -Expected 0 -Actual (Get-GizmoInfo).Segments -Message 'both off draws nothing'
}

Test 'force gizmos: a deleted (parked) entity is not drawn' {
    SendOk 'force_gizmos all', 'select fan', 'delete' | Out-Null
    $info = Get-GizmoInfo
    Assert-Equal -Expected 0 -Actual $info.Forces -Message 'the only field is gone with the entity'
    Assert-True -Condition (-not $info.Markers.ContainsKey('fan')) -Message 'and has no marker'
    SendOk 'undo' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    Assert-Equal -Expected 1 -Actual (Get-GizmoInfo).Forces -Message 'undo brings it back'
}

Test 'the gizmos render without disturbing the scene, and nothing crashed' {
    SendOk 'platform_gizmos off', 'force_gizmos off', 'select' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $plain = Shot 'gizmos-off'
    SendOk 'platform_gizmos all', 'force_gizmos all' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $drawn = Shot 'gizmos-all'
    # The overlay goes into ImGui's background draw list, so it changes pixels but must
    # not change the render: a wholesale difference would mean it disturbed the frame.
    $diff = Get-ImageDifference -PathA $plain -PathB $drawn -Step 2 -Threshold 30
    Assert-True -Condition ($diff.DifferingShare -gt 0.0005) `
        -Message "the overlay actually drew something (differing share $($diff.DifferingShare))"
    Assert-True -Condition ($diff.DifferingShare -lt 0.35) `
        -Message "and only lines, not the whole frame (differing share $($diff.DifferingShare))"
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

# ---- persistence ----------------------------------------------------------------------

Test 'the authored values survive a save' {
    SendOk 'select box_a' | Out-Null
    # Plain Send: the tests above have already given box_a a Platform, and this has to
    # stand on its own when the file is run with -Test.
    Send 'add_component box_a Platform' | Out-Null
    SendOk "set_component box_a Platform ""{'linear_dir':{'x':0,'y':1,'z':0},'amplitude':3.5,'freq':0.5,'phase':0.125,'angular_speed':1.25,'delay':0.75,'fall_delay':6}""" | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content -Raw -Path $LevelPath | ConvertFrom-Json
    # box_a is a placed instance, so its per-entity component delta lands in its instance
    # record; an FBX-authored entity's would land in `entities` instead. Either is a
    # record of its own, which is the thing being asserted.
    $record = @($level.world.instances) + @($level.world.entities) |
        Where-Object { $_.name -eq 'box_a' -and $null -ne $_.components.Platform } |
        Select-Object -First 1
    Assert-True -Condition ($null -ne $record) `
        -Message 'box_a has a record of its own carrying its Platform block'
    $block = $record.components.Platform
    Assert-Near -Expected 3.5 -Actual $block.amplitude -Tolerance 0.0001
    Assert-Near -Expected 0.5 -Actual $block.freq -Tolerance 0.0001
    Assert-Near -Expected 0.125 -Actual $block.phase -Tolerance 0.0001
    Assert-Near -Expected 1.25 -Actual $block.angular_speed -Tolerance 0.0001
    Assert-Near -Expected 0.75 -Actual $block.delay -Tolerance 0.0001
    Assert-Near -Expected 6 -Actual $block.fall_delay -Tolerance 0.0001
    # Live state is not authoring data and must not be written, or a save would freeze a
    # mid-swing snapshot into the level.
    Assert-True -Condition ($null -eq $block.rt) -Message 'no runtime state in the file'
    Assert-True -Condition ($null -eq $block.clock) -Message 'and no clock'
}
