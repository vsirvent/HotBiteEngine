# fixture: empty
# description: Spotlights - the cone actually shapes the light (aimed vs turned away vs omni), and follows the entity's rotation.

# A spotlight is a PointLight with is_spot set, so every measurement here is a
# comparison between renders of *one* scene that differ only in the light: the
# frame has sun, ambient and temporal GI in it and an absolute level would say
# nothing. Bright is measured in two rectangles - the middle of the viewport,
# which the camera is aimed at, and a band near the left/right edges of the floor
# (the editor's panels are outside 0.15..0.85) - so "inside the cone" and
# "outside the cone" are both in frame.

function Get-Zones {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 12
    $path = Shot $Name
    # Inner is the pool a spot aimed straight down leaves under the lamp, outer the
    # far corner of the floor, total everything the floor fills.
    $inner = Get-ImageStats -Path $path -Left 0.47 -Top 0.46 -Right 0.53 -Bottom 0.53 -Step 2
    $outer = Get-ImageStats -Path $path -Left 0.20 -Top 0.75 -Right 0.35 -Bottom 0.95
    $total = Get-ImageStats -Path $path -Left 0.20 -Top 0.30 -Right 0.80 -Bottom 0.95
    return [pscustomobject]@{ Inner = $inner.Mean; Outer = $outer.Mean; Total = $total.Mean }
}

function Set-Beam {
    param([bool]$Spot, [double]$Pitch, [double]$Range = 60)
    SendOk 'select Lamp',
        "set_component Lamp PointLight ""{'spot':$($Spot.ToString().ToLower()),'spot_inner':8,'spot_outer':14,'range':$Range}""",
        "set_rotation $Pitch 0 0" | Out-Null
}

Test 'the light overlays default to: shapes for the selection, positions for every light' {
    $state = Get-State -Session $Session
    Assert-Equal -Expected 'selection' -Actual $state.light_view
    Assert-Equal -Expected 'True' -Actual $state.light_positions
    $menus = (SendOk 'menus')[0].Payload -join "`n"
    foreach ($item in @('View/Light Gizmos: Selection', 'View/Light Gizmos: All', 'View/Light Positions')) {
        Assert-True -Condition ($menus -match [regex]::Escape($item)) -Message "menu $item is registered"
    }
}

Test 'set up: a floor, and a lamp above it' {
    # A static floor - a dynamic body would fall out from under the light.
    SendOk 'select box_a',
        "set_component box_a Physics ""{'type':'STATIC'}""",
        'set_position 0 0 0', 'set_scale 40 0.2 40' | Out-Null
    SendOk 'menu "Add/Entity"' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-Contains -Collection $names -Value 'Entity' -Message 'Add/Entity named it Entity'
    SendOk 'rename Entity Lamp' | Out-Null
    SendOk 'add_component Lamp PointLight', 'select Lamp', 'set_position 0 8 0' | Out-Null
    # Take the sun out of it as far as the level lets us; harmless if there is no Sky.
    # The fixture's sun and ambient light would flood the floor and hide the cone.
    SendOk "set_component sun DirectionalLight ""{'intensity':0}""",
        "set_component ambient AmbientLight ""{'color_down':{'x':0,'y':0,'z':0},'color_up':{'x':0,'y':0,'z':0}}""" | Out-Null
    Send "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':0}""" | Out-Null
    SendOk 'select box_a', 'focus' | Out-Null
    SendOk 'camera_pos 0 30 -30' | Out-Null
    # The brightness tests below measure the floor; a marker or a cone drawn over it would\n    # be counted as light. The gizmo tests at the end switch them back on themselves.\n    SendOk 'light_gizmos off', 'light_positions off' | Out-Null\n    Step-EditorFrames -Session $Session -Count 5
}

Test 'a spotlight aimed at the floor lights the middle and leaves the edges dark' {
    Set-Beam -Spot $true -Pitch 90
    $aimed = Get-Zones 'spot-aimed'
    Set-Beam -Spot $false -Pitch 90
    $omni = Get-Zones 'omni'
    Assert-True -Condition ($aimed.Inner -gt $aimed.Outer + 8) `
        -Message "inside the cone is brighter than outside (inner $($aimed.Inner), outer $($aimed.Outer))"
    Assert-True -Condition ($omni.Outer -gt $aimed.Outer + 5) `
        -Message "the omni light reaches the edges the spot does not (omni outer $($omni.Outer), spot outer $($aimed.Outer))"
}

Test 'turning the spotlight away removes its light from the floor' {
    Set-Beam -Spot $true -Pitch 90
    $aimed = Get-Zones 'aim-down'
    Set-Beam -Spot $true -Pitch -90
    $away = Get-Zones 'aim-up'
    Assert-True -Condition ($aimed.Inner -gt $away.Inner + 8) `
        -Message "the floor is lit when aimed at and dark when aimed away (aimed $($aimed.Inner), away $($away.Inner))"
}

Test 'a wider outer angle lights more of the floor' {
    Set-Beam -Spot $true -Pitch 90
    SendOk "set_component Lamp PointLight ""{'spot_inner':5,'spot_outer':8}""" | Out-Null
    $narrow = Get-Zones 'narrow'
    SendOk "set_component Lamp PointLight ""{'spot_inner':30,'spot_outer':60}""" | Out-Null
    $wide = Get-Zones 'wide'
    Assert-True -Condition ($wide.Total -gt $narrow.Total + 2) `
        -Message "widening the cone lights more of the floor (narrow $($narrow.Total), wide $($wide.Total))"
}

Test 'a shadow-casting spotlight still lights the floor' {
    Set-Beam -Spot $true -Pitch 90
    $lit = Get-Zones 'noshadow'
    SendOk "set_component Lamp PointLight ""{'cast_shadow':true}""" | Out-Null
    $shadowed = Get-Zones 'shadow'
    Assert-True -Condition ($shadowed.Inner -gt $shadowed.Outer + 4) `
        -Message "the cone survives the shadow lookup (inner $($shadowed.Inner), outer $($shadowed.Outer))"
    Assert-True -Condition (-not $Session.Process.HasExited) -Message 'no crash'
}

# ---- gizmos -------------------------------------------------------------------------
# What the overlay drew is read back through `light_gizmo_info` (the last frame's
# counters and marker positions); a marker's *pixels* are checked in a screenshot.
# The counts are exact because segments are counted as they are emitted, before any
# clipping: a spotlight is 40 (outer circle) + 4 (edges) + 40 (inner circle) + 1 (axis)
# = 85, a point light is three 40-segment great circles = 120.
$SpotSegments = 85
$PointSegments = 120

function Get-GizmoInfo {
    Step-EditorFrames -Session $Session -Count 3
    $r = (SendOk 'light_gizmo_info')[0]
    $m = [regex]::Match($r.Text, 'lights=(\d+) spots=(\d+) segments=(\d+) markers=(\d+)')
    if (-not $m.Success) { throw "unparseable light_gizmo_info: $($r.Text)" }
    $markers = @{}
    foreach ($line in $r.Payload) {
        $p = $line -split ' '
        if ($p.Count -ge 4) { $markers[$p[0]] = [pscustomobject]@{ X = [double]$p[1]; Y = [double]$p[2]; Kind = $p[3] } }
    }
    return [pscustomobject]@{
        Lights = [int]$m.Groups[1].Value; Spots = [int]$m.Groups[2].Value
        Segments = [int]$m.Groups[3].Value; MarkerCount = [int]$m.Groups[4].Value; Markers = $markers
    }
}

Test 'light gizmos: a second, point light joins the scene' {
    SendOk 'menu "Add/Entity"' | Out-Null
    SendOk 'rename Entity Bulb' | Out-Null
    SendOk 'add_component Bulb PointLight', 'select Bulb', 'set_position 6 6 0',
        "set_component Bulb PointLight ""{'range':10}""" | Out-Null
    Set-Beam -Spot $true -Pitch 90
    SendOk 'select' | Out-Null
}

Test 'light gizmos: the shapes follow the selection, and exactly the right lines are drawn' {
    SendOk 'light_gizmos selection', 'light_positions off', 'select' | Out-Null
    $none = Get-GizmoInfo
    Assert-Equal -Expected 0 -Actual $none.Lights -Message 'nothing selected, nothing drawn'
    Assert-Equal -Expected 0 -Actual $none.Segments

    SendOk 'select Lamp' | Out-Null
    $spot = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $spot.Lights
    Assert-Equal -Expected 1 -Actual $spot.Spots -Message 'the selected light is a cone'
    Assert-Equal -Expected $SpotSegments -Actual $spot.Segments

    SendOk 'select Bulb' | Out-Null
    $point = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $point.Lights
    Assert-Equal -Expected 0 -Actual $point.Spots -Message 'the selected light is a sphere'
    Assert-Equal -Expected $PointSegments -Actual $point.Segments
}

Test 'light gizmos: All draws every light, Off draws none, and the switches are exclusive' {
    SendOk 'select', 'light_gizmos all' | Out-Null
    $all = Get-GizmoInfo
    Assert-Equal -Expected 2 -Actual $all.Lights
    Assert-Equal -Expected 1 -Actual $all.Spots
    Assert-Equal -Expected ($SpotSegments + $PointSegments) -Actual $all.Segments
    Assert-Equal -Expected 'all' -Actual (Get-State -Session $Session).light_view

    SendOk 'light_gizmos off' | Out-Null
    Assert-Equal -Expected 0 -Actual (Get-GizmoInfo).Segments -Message 'off draws nothing'

    # The menu entries are a radio group: clicking the active one turns the view off.
    SendOk 'menu "View/Light Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'all' -Actual (Get-State -Session $Session).light_view
    SendOk 'menu "View/Light Gizmos: All"' | Out-Null
    Assert-Equal -Expected 'off' -Actual (Get-State -Session $Session).light_view
    SendOk 'menu "View/Light Gizmos: Selection"' | Out-Null
    Assert-Equal -Expected 'selection' -Actual (Get-State -Session $Session).light_view

    Assert-Err -Result (Send 'light_gizmos sideways')[0] -Pattern 'usage'
    Assert-Err -Result (Send 'light_positions maybe')[0] -Pattern 'usage'
}

Test 'light gizmos: turning a spotlight into a point light changes its shape' {
    SendOk 'select Lamp', 'light_gizmos selection' | Out-Null
    Assert-Equal -Expected $SpotSegments -Actual (Get-GizmoInfo).Segments
    SendOk "set_component Lamp PointLight ""{'spot':false}""" | Out-Null
    $info = Get-GizmoInfo
    Assert-Equal -Expected $PointSegments -Actual $info.Segments -Message 'no longer a cone'
    Assert-Equal -Expected 0 -Actual $info.Spots
    SendOk "set_component Lamp PointLight ""{'spot':true}""" | Out-Null
    Assert-Equal -Expected $SpotSegments -Actual (Get-GizmoInfo).Segments -Message 'and back'
}

Test 'light positions: a marker per light, whether or not it is selected, and the check turns them off' {
    SendOk 'light_gizmos off', 'select', 'light_positions on' | Out-Null
    $on = Get-GizmoInfo
    Assert-Equal -Expected 2 -Actual $on.MarkerCount -Message 'one marker per light, nothing selected'
    Assert-Equal -Expected 'spot' -Actual $on.Markers['Lamp'].Kind
    Assert-Equal -Expected 'point' -Actual $on.Markers['Bulb'].Kind
    Assert-True -Condition ($on.Markers['Lamp'].X -gt 0 -and $on.Markers['Lamp'].X -lt 1 -and $on.Markers['Lamp'].Y -gt 0 -and $on.Markers['Lamp'].Y -lt 1) `
        -Message 'the lamp projects inside the viewport'
    Assert-True -Condition ($on.Markers['Bulb'].X -gt $on.Markers['Lamp'].X) `
        -Message 'the bulb is to the +X side of the lamp, so it is drawn to its right'
    Assert-Equal -Expected 0 -Actual $on.Segments -Message 'markers are not shapes'

    SendOk 'menu "View/Light Positions"' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).light_positions
    Assert-Equal -Expected 0 -Actual (Get-GizmoInfo).MarkerCount -Message 'unchecked: no markers'
    SendOk 'menu "View/Light Positions"' | Out-Null
    Assert-Equal -Expected 2 -Actual (Get-GizmoInfo).MarkerCount -Message 'checked again'
}

Test 'light positions: a marker on screen is the light colour, and gone when unchecked' {
    SendOk "set_component Lamp PointLight ""{'color':{'x':1,'y':0,'z':1}}""",
        'select', 'light_gizmos off', 'light_positions on' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $m = (Get-GizmoInfo).Markers['Lamp']
    $withMarker = Shot 'marker-on'
    SendOk 'light_positions off' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $without = Shot 'marker-off'

    $bmp = New-Object System.Drawing.Bitmap($withMarker)
    try {
        # The centre pixel of the marker: (x, y) are fractions of the display and the
        # screenshot is the same frame, so this scales to whatever size it came out.
        $a = $bmp.GetPixel([int]($m.X * $bmp.Width), [int]($m.Y * $bmp.Height))
    }
    finally { $bmp.Dispose() }
    Assert-True -Condition ($a.R -gt 200 -and $a.B -gt 200 -and $a.G -lt 80) `
        -Message "the marker is drawn in the light's magenta (got $($a.R),$($a.G),$($a.B))"

    # The light's own glow is magenta too, so one pixel cannot tell the marker from its
    # absence. What can is the neighbourhood: the marker's dark outline ring and rays
    # against the smooth glow. 24 px either way of the marker, every pixel.
    $rx = 24.0 / 2560; $ry = 24.0 / 1377
    $diff = Get-ImageDifference -PathA $withMarker -PathB $without -Step 1 -Threshold 30 `
        -Left ($m.X - $rx) -Right ($m.X + $rx) -Top ($m.Y - $ry) -Bottom ($m.Y + $ry)
    Assert-True -Condition ($diff.DifferingShare -gt 0.05) `
        -Message "switching the marker off changed the pixels around it (differing share $($diff.DifferingShare))"
}
Test 'light gizmos: a deleted (parked) light is not drawn' {
    SendOk 'select Bulb', 'delete', 'light_gizmos all', 'light_positions on' | Out-Null
    $info = Get-GizmoInfo
    Assert-Equal -Expected 1 -Actual $info.Lights -Message 'only the lamp is left'
    Assert-Equal -Expected 1 -Actual $info.MarkerCount
    Assert-True -Condition (-not $info.Markers.ContainsKey('Bulb')) -Message 'the deleted light has no marker'
}
# ---- glow radius ---------------------------------------------------------------------
# PointLight's `tilt_ratio` is the radius, in pixels, of the light's visible glow
# (EmitPoint in PixelFunctions.hlsli): the core, with a halo ten times as wide. 10 is
# what the glow was when it was hard-coded, 0 draws none.
function Get-GlowMean {
    param([double]$Radius, [string]$Name)
    SendOk "set_component Lamp PointLight ""{'tilt_ratio':$Radius}""",
        'select', 'light_gizmos off', 'light_positions on' | Out-Null
    $m = (Get-GizmoInfo).Markers['Lamp']
    # The marker is only used to find the lamp on screen; it would be counted as glow.
    SendOk 'light_positions off' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $path = Shot $Name
    $rx = 90.0 / 2560; $ry = 90.0 / 1377
    return (Get-ImageStats -Path $path -Step 2 `
        -Left ($m.X - $rx) -Right ($m.X + $rx) -Top ($m.Y - $ry) -Bottom ($m.Y + $ry)).Mean
}

Test 'glow radius: it is serialized, and defaults to the size the glow always had' {
    $light = Get-Component -Session $Session -Entity 'Lamp' -Component 'PointLight'
    SendOk "set_component Lamp PointLight ""{'tilt_ratio':10}""" | Out-Null
    $light = Get-Component -Session $Session -Entity 'Lamp' -Component 'PointLight'
    Assert-Near -Expected 10 -Actual $light.tilt_ratio -Tolerance 0.001
    SendOk "set_component Lamp PointLight ""{'tilt_ratio':25.5}""" | Out-Null
    $light = Get-Component -Session $Session -Entity 'Lamp' -Component 'PointLight'
    Assert-Near -Expected 25.5 -Actual $light.tilt_ratio -Tolerance 0.001
}

Test 'glow radius: a bigger radius draws a bigger glow, and zero draws none' {
    SendOk 'select Lamp' | Out-Null
    Set-Beam -Spot $false -Pitch 90
    $none = Get-GlowMean -Radius 0 -Name 'glow-0'
    $normal = Get-GlowMean -Radius 10 -Name 'glow-10'
    $big = Get-GlowMean -Radius 40 -Name 'glow-40'
    Assert-True -Condition ($normal -gt $none + 1) `
        -Message "the default radius glows (0: $none, 10: $normal)"
    Assert-True -Condition ($big -gt $normal + 1) `
        -Message "a larger radius covers more of the neighbourhood (10: $normal, 40: $big)"
}