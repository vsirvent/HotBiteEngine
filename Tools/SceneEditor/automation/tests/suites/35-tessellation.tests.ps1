# fixture: empty
# description: A height map displaces a tessellated material by default - the four tessellation modes (off, on, distance, silhouette), the non-zero default displacement, that an explicit 0 still means "off", and what reaches the frame, on a flat test map and on a real PBR texture set (ambientCG Bricks076C, kept under tests\assets).

$png = Join-Path $Assets 'materials\test_red.png'
$pbrDir = Join-Path $repoRoot 'Tools\SceneEditor\automation\tests\assets\Bricks076C_1K'

# name -> the value a material stores (tess_type). Listing order is the editor's; the
# numbers are not in that order because silhouette (1) and distance (2) predate "on" (3).
$modes = [ordered]@{ off = 0; on = 3; distance = 2; silhouette = 1 }

# "OK tess=<mode> factor=<f> displacement=<d> height=<yes|no>" without the OK.
function Surface {
    param([string]$Material)
    return (SendOk "material_surface $Material")[0].Text
}

# A cube at the origin with the camera a fixed distance away: 3 units seen from ~10 by
# default (the "near" view), or any scale and camera for a "far" one. Transform edits reach
# the render on the background tick and a commanded camera pose can be lost, not just
# late (see tests\README.md), so wait for the rendered camera as 19-multimaterials'
# Set-SlabView does. The other boxes stay where they are: they read the same in every
# shot, so they only move the baseline.
function Set-BoxView {
    param([double]$Scale = 3.0, [double[]]$Camera = @(-3.0, 4.0, -9.0))
    SendOk 'select box_a', "set_scale $Scale $Scale $Scale", 'set_position 0 0 0',
           "camera_pos $($Camera -join ' ')", 'camera_target 0 0 0' | Out-Null
    Start-Sleep -Milliseconds 400
    for ($i = 0; $i -lt 30; $i += 2) {
        $cam = Get-Camera -Session $Session
        $d = 0.0
        for ($k = 0; $k -lt 3; $k++) { $d += [Math]::Pow($cam.world_position[$k] - $Camera[$k], 2) }
        if ([Math]::Sqrt($d) -le 0.2) {
            # Long enough for the GI/denoiser history to settle on the new view: the
            # first comparison below is against a floor, and a floor still converging
            # reads as a difference between two shots of one state.
            Step-EditorFrames -Session $Session -Count 40
            return
        }
        Step-EditorFrames -Session $Session -Count 2
    }
    throw "the camera never reached the box view (at $((Get-Camera -Session $Session).world_position -join ', '))"
}

# A screenshot once the material change has had frames to reach the render.
function Take {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 4
    return Shot $Name
}

# Share of the middle of the viewport (panels excluded) that differs between two shots.
# The engine accumulates temporally, so two shots of one state never match exactly:
# every comparison is made against that floor - the same state shot twice - rather than
# against zero.
function Differ {
    param([string]$A, [string]$B,
          [double]$Left = 0.25, [double]$Top = 0.15, [double]$Right = 0.75, [double]$Bottom = 0.85)
    return (Get-ImageDifference -PathA $A -PathB $B -Left $Left -Top $Top -Right $Right -Bottom $Bottom -Step 2).DifferingShare
}

# Where the faces of the near view sit in the window (see Set-BoxView's defaults): the big
# one facing the camera, and the top one seen at a glancing angle.
$frontFace = @{ Left = 0.45; Top = 0.45; Right = 0.60; Bottom = 0.72 }
$topFace = @{ Left = 0.40; Top = 0.29; Right = 0.62; Bottom = 0.39 }

function Pct { param([double]$Share) return [Math]::Round($Share * 100, 2) }

# The GPU's own count of tessellated vertices (domain shader invocations) for the whole
# scene pass - the only direct evidence of what the hull shader decided (see
# RenderSystem::TessStats). Asking turns the query on, and a reading is a few frames stale,
# so: ask, let frames pass, and take the value once two consecutive readings agree.
# Only meaningful as a comparison between states of one scene: every untessellated patch
# in the level counts too, which is the same constant in every state.
function Get-TessCount {
    SendOk 'tess_info' | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    $last = -1
    for ($i = 0; $i -lt 30; $i++) {
        $r = (SendOk 'tess_info')[0].Text
        if ($r -match 'valid=1 .* ds=(\d+)') {
            $ds = [long]$Matches[1]
            if ($ds -eq $last) { return $ds }
            $last = $ds
        }
        Step-EditorFrames -Session $Session -Count 3
    }
    throw "the tessellation count never settled (last answer: $r)"
}

# Share of a rectangle (fractions of the window) that is near-black. Inside a face of the
# cube nothing is that dark - the bricks' darkest mortar is well above it - so what is
# there is background showing through: a crack.
function Get-DarkShare {
    param([string]$Path, [double]$Left, [double]$Top, [double]$Right, [double]$Bottom, [int]$Threshold = 6)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($Path)
    try {
        $n = 0; $dark = 0
        for ($y = [int]($bmp.Height * $Top); $y -lt [int]($bmp.Height * $Bottom); $y++) {
            for ($x = [int]($bmp.Width * $Left); $x -lt [int]($bmp.Width * $Right); $x++) {
                $p = $bmp.GetPixel($x, $y)
                $n++
                if ([Math]::Max($p.R, [Math]::Max($p.G, $p.B)) -le $Threshold) { $dark++ }
            }
        }
        return $dark / [double]$n
    }
    finally { $bmp.Dispose() }
}

# --- the data: what a material carries -------------------------------------------

Test 'a new material has displacement ready and tessellation off' {
    SendOk 'create_material Relief materials\test.mat' | Out-Null
    Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.100000 height=no' -Actual (Surface Relief)
}

Test 'a material saved with an explicit displacement of 0 keeps it off' {
    # The fixture's .mat files write displacement_scale = 0.0 for every material, as every
    # saved .mat does: only a material that never set the value gets the default.
    Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.000000 height=no' -Actual (Surface TestRed)
}

Test 'material_surface sets the mode, factor and displacement in one step' {
    Assert-Equal -Expected 'tess=distance factor=16.000000 displacement=0.100000 height=no' `
        -Actual (SendOk 'material_surface Relief distance 16')[0].Text
    Assert-Equal -Expected 'tess=silhouette factor=8.000000 displacement=0.250000 height=no' `
        -Actual (SendOk 'material_surface Relief silhouette 8 0.25')[0].Text
    Assert-Equal -Expected 'tess=silhouette factor=8.000000 displacement=0.100000 height=no' `
        -Actual (SendOk 'material_surface Relief 1 8 default')[0].Text -Message 'a number names the mode too, and "default" restores the displacement'
}

Test 'every mode is reachable by name, in any case, and by its stored number' {
    foreach ($mode in $modes.Keys) {
        Assert-Match -Pattern "^tess=$mode " -Actual (SendOk "material_surface Relief $mode 4")[0].Text -Message "by name: $mode"
        Assert-Match -Pattern "^tess=$mode " -Actual (SendOk "material_surface Relief $($mode.ToUpper()) 4")[0].Text -Message "in capitals: $mode"
        Assert-Match -Pattern "^tess=$mode " -Actual (SendOk "material_surface Relief $($modes[$mode]) 4")[0].Text -Message "by number: $($modes[$mode])"
    }
}

Test 'off is the default mode, and the other three are each their own' {
    SendOk 'create_material Fresh materials\test.mat' | Out-Null
    Assert-Match -Pattern '^tess=off ' -Actual (Surface Fresh) -Message 'a new material tessellates nothing'
    $seen = @{}
    foreach ($mode in $modes.Keys) {
        $seen[(SendOk "material_surface Fresh $mode 4")[0].Text -replace ' .*$', ''] = $true
    }
    Assert-Equal -Expected 4 -Actual $seen.Count -Message 'four modes, four distinct answers'
    SendOk 'material_surface Fresh off 0' | Out-Null
}

Test 'material_surface is undoable, for every mode' {
    SendOk 'material_surface Relief off 0 0.1' | Out-Null
    foreach ($mode in 'on', 'distance', 'silhouette') {
        SendOk "material_surface Relief $mode 16 0.3" | Out-Null
        Assert-Match -Pattern "^tess=$mode factor=16" -Actual (Surface Relief)
        SendOk 'undo' | Out-Null
        Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.100000 height=no' -Actual (Surface Relief) -Message "undo of $mode"
        SendOk 'redo' | Out-Null
        Assert-Match -Pattern "^tess=$mode factor=16" -Actual (Surface Relief) -Message "redo of $mode"
        SendOk 'undo' | Out-Null
    }
}

Test 'material_surface rejects what it cannot apply, and changes nothing' {
    SendOk 'material_surface Relief distance 16 0.3' | Out-Null
    Assert-Err -Result (Send 'material_surface Nope off')[0] -Pattern 'material not found'
    Assert-Err -Result (Send 'material_surface Relief sideways')[0] -Pattern 'unknown tessellation mode: sideways \(off on distance silhouette\)'
    Assert-Err -Result (Send 'material_surface Relief 4')[0] -Pattern 'unknown tessellation mode'
    Assert-Err -Result (Send 'material_surface Relief distance lots')[0] -Pattern 'must be numbers'
    Assert-Err -Result (Send 'material_surface')[0] -Pattern 'usage'
    Assert-Equal -Expected 'tess=distance factor=16.000000 displacement=0.300000 height=no' -Actual (Surface Relief)
}

Test 'a height map goes in the slot like any imported texture' {
    SendOk "import_texture $png Relief" | Out-Null
    SendOk 'set_material_texture Relief height Relief\test_red.png' | Out-Null
    Assert-Match -Pattern 'height=yes$' -Actual (Surface Relief)
}

Test 'every mode is written to the .mat file as the number MainRenderVS reads' {
    foreach ($mode in $modes.Keys) {
        SendOk "material_surface Relief $mode 12 0.25" | Out-Null
        SendOk 'save_materials' | Out-Null
        $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
        $saved = $mat.materials | Where-Object { $_.name -eq 'Relief' }
        Assert-Equal -Expected $modes[$mode] -Actual $saved.tess_type -Message "tess_type for $mode"
        Assert-Near -Expected 12.0 -Actual $saved.tess_factor -Tolerance 0.001
        Assert-Near -Expected 0.25 -Actual $saved.displacement_scale -Tolerance 0.001
    }
    Assert-Match -Pattern 'test_red\.png$' -Actual $saved.high_textname
}

Test 'a displacement of 0 is written as 0, not dropped for the default' {
    SendOk 'material_surface Relief silhouette 12 0' | Out-Null
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
    $saved = $mat.materials | Where-Object { $_.name -eq 'Relief' }
    Assert-Near -Expected 0.0 -Actual $saved.displacement_scale -Tolerance 0.0001 `
        -Message 'a saved 0 has to survive, or "off" would come back as the default on the next load'
}

# --- the frame, on a flat map: every mode displaces, off does not ---------------------
# Displaced faces stand out from the cube (the cube's faces carry one normal each, so
# they slide apart), which moves a large share of the middle of the view; the same state
# shot twice moves almost none of it.

Test 'off displaces nothing, however much factor and map the material has' {
    SendOk 'set_material box_a Relief' | Out-Null
    Set-BoxView
    SendOk 'material_surface Relief off 0 default' | Out-Null
    $plain = Take 'flat-off'
    $plainAgain = Take 'flat-off-again'
    SendOk 'material_surface Relief off 16 default' | Out-Null
    $withFactor = Take 'flat-off-factor'
    $floor = Differ $plain $plainAgain
    $moved = Differ $plain $withFactor
    Assert-True -Condition ($floor -lt 0.02) -Message "the same state shot twice differs by $(Pct $floor) % - the view is not settled"
    Assert-True -Condition ($moved -lt 0.02) `
        -Message "mode off with a factor of 16, a height map and a default displacement must draw flat, but $(Pct $moved) % of the view changed"
}

foreach ($mode in 'on', 'distance', 'silhouette') {
    Test "mode $mode displaces a flat height map by default" {
        SendOk 'material_surface Relief off 0 default' | Out-Null
        $off = Take "flat-off-vs-$mode"
        SendOk "material_surface Relief $mode 16 default" | Out-Null
        $on = Take "flat-$mode"
        $moved = Differ $off $on
        # Measured: floor 0 %, distance 5.5 %. 2 % is well clear of both sides.
        Assert-True -Condition ($moved -gt 0.02) `
            -Message "mode $mode with the default displacement should move the surface, but only $(Pct $moved) % of the view changed"
    }
}

Test 'an explicit displacement of 0 turns every mode back off' {
    SendOk 'material_surface Relief off 0 default' | Out-Null
    $off = Take 'zero-off'
    foreach ($mode in 'on', 'distance', 'silhouette') {
        SendOk "material_surface Relief $mode 16 0" | Out-Null
        $zero = Take "zero-$mode"
        $moved = Differ $off $zero
        Assert-True -Condition ($moved -lt 0.02) `
            -Message "mode $mode with displacement 0 should look like tessellation off, but $(Pct $moved) % of the view changed"
    }
}

Test 'without a height map, no mode displaces anything' {
    SendOk 'material_surface Relief off 0 default' | Out-Null
    SendOk 'set_material_texture Relief height none' | Out-Null
    $off = Take 'nomap-off'
    $failure = $null
    foreach ($mode in 'on', 'distance', 'silhouette') {
        SendOk "material_surface Relief $mode 16 default" | Out-Null
        $moved = Differ $off (Take "nomap-$mode")
        if ($moved -ge 0.02 -and $null -eq $failure) {
            $failure = "mode $mode changed $(Pct $moved) % of the view with no height map to act on"
        }
    }
    # Restored before asserting, so one failure does not leave the map off for the rest.
    SendOk 'set_material_texture Relief height Relief\test_red.png' | Out-Null
    Assert-True -Condition ($null -eq $failure) -Message $failure
}

Test 'the displacement is a real setting, so a larger one moves the surface further' {
    SendOk 'material_surface Relief on 16 default' | Out-Null
    $default = Take 'disp-default'
    SendOk 'material_surface Relief on 16 0.4' | Out-Null
    $larger = Take 'disp-larger'
    $moved = Differ $default $larger
    Assert-True -Condition ($moved -gt 0.03) `
        -Message "0.4 against the default changed $(Pct $moved) % of the view"
}

# --- the frame, on a real PBR texture: ambientCG Bricks076C (tests\assets) -----------
# A flat image has no relief, so it can only show *that* the surface moved. A real height
# map is what tessellation is for: bricks and mortar at different depths.

Test 'a real PBR set imports whole and fills every slot the engine has for it' {
    Assert-True -Condition (Test-Path $pbrDir) -Message "the test texture set is missing: $pbrDir"
    $r = SendOk "import_texture_folder `"$pbrDir`" Bricks"
    Assert-Match -Pattern '^4 imported, 1 skipped' -Actual $r[0].Text -Message 'four maps; SOURCE.txt is not an image'
    SendOk 'create_material Bricks materials\test.mat' | Out-Null
    SendOk 'set_material_texture Bricks diffuse Bricks\Bricks076C_1K_Color.jpg',
           'set_material_texture Bricks normal Bricks\Bricks076C_1K_NormalDX.jpg',
           'set_material_texture Bricks height Bricks\Bricks076C_1K_Displacement.jpg',
           'set_material_texture Bricks ao Bricks\Bricks076C_1K_AmbientOcclusion.jpg' | Out-Null
    $slots = @((SendOk 'textures Bricks')[0].Payload)
    foreach ($slot in 'diffuse', 'normal', 'height', 'ao') {
        Assert-True -Condition ([bool]@($slots -match "^$slot=Bricks\\Bricks076C_1K_")) -Message "the $slot slot names the imported map"
    }
    Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.100000 height=yes' -Actual (Surface Bricks) `
        -Message 'a real set on a new material: default displacement ready, tessellation off'
}

Test 'the real texture renders as bricks on the cube' {
    SendOk 'set_material box_a Bricks' | Out-Null
    Set-BoxView
    SendOk 'material_surface Bricks off 0 default' | Out-Null
    $shot = Take 'bricks-off'
    # The front face: bricks are red-brown, so red leads blue, and it is not a black hole.
    $face = Get-ImageStats -Path $shot -Left 0.42 -Top 0.45 -Right 0.58 -Bottom 0.62
    Assert-True -Condition ($face.LitShare -gt 0.9) -Message "the face sample is $(Pct $face.LitShare) % lit - the framing moved"
    Assert-True -Condition ($face.MeanR -gt $face.MeanB * 1.15) `
        -Message "bricks should read red over blue, got R=$([Math]::Round($face.MeanR, 1)) B=$([Math]::Round($face.MeanB, 1))"
}

foreach ($mode in 'on', 'distance', 'silhouette') {
    Test "mode $mode displaces the real height map" {
        SendOk 'material_surface Bricks off 0 default' | Out-Null
        $off = Take "bricks-off-vs-$mode"
        SendOk "material_surface Bricks $mode 16 default" | Out-Null
        $shot = Take "bricks-$mode"
        $moved = Differ $off $shot
        Assert-True -Condition ($moved -gt 0.02) `
            -Message "mode $mode with the default displacement should move the brick surface, but only $(Pct $moved) % of the view changed"
        # Still a textured surface, not torn to black by the displacement.
        $face = Get-ImageStats -Path $shot -Left 0.42 -Top 0.45 -Right 0.58 -Bottom 0.62
        Assert-True -Condition ($face.LitShare -gt 0.5) -Message "the face went dark under mode $mode ($(Pct $face.LitShare) % lit)"
    }
}

Test 'off draws the real texture flat even with a factor set' {
    SendOk 'material_surface Bricks off 0 default' | Out-Null
    $plain = Take 'bricks-plain'
    SendOk 'material_surface Bricks off 16 default' | Out-Null
    $withFactor = Take 'bricks-off-factor'
    $moved = Differ $plain $withFactor
    Assert-True -Condition ($moved -lt 0.02) -Message "mode off changed $(Pct $moved) % of the view because of a factor it must ignore"
}

Test 'an explicit displacement of 0 keeps every mode flat on the real texture' {
    SendOk 'material_surface Bricks off 0 default' | Out-Null
    $off = Take 'bricks-zero-off'
    foreach ($mode in 'on', 'distance', 'silhouette') {
        SendOk "material_surface Bricks $mode 16 0" | Out-Null
        $moved = Differ $off (Take "bricks-zero-$mode")
        Assert-True -Condition ($moved -lt 0.02) -Message "mode $mode with displacement 0 changed $(Pct $moved) % of the view"
    }
}

# --- the modes are different modes, not one mode under four names --------------------
# Up to here every mode was only shown to displace *something*. These pin down how they
# differ. Silhouette is the regression test for a real bug: the vertex shader was handed
# the camera's look-at *point* as its view direction, which is zero when the camera looks
# at the origin, so silhouette came out identical to distance whenever it did.

Test 'silhouette leaves the face toward the camera flat and displaces the faces glancing away' {
    SendOk 'set_material box_a Bricks' | Out-Null
    Set-BoxView
    SendOk 'material_surface Bricks off 16 default' | Out-Null
    $off = Take 'faces-off'
    SendOk 'material_surface Bricks silhouette 16 default' | Out-Null
    $silhouette = Take 'faces-silhouette'
    SendOk 'material_surface Bricks on 16 default' | Out-Null
    $on = Take 'faces-on'
    $frontKept = Differ $off $silhouette @frontFace
    $frontMoved = Differ $off $on @frontFace
    $topMoved = Differ $off $silhouette @topFace
    Assert-True -Condition ($frontKept -lt 0.02) `
        -Message "silhouette should leave the face looking at the camera alone, but $(Pct $frontKept) % of it changed"
    Assert-True -Condition ($frontMoved -gt 0.05) `
        -Message "on should displace that same face, but only $(Pct $frontMoved) % of it changed"
    Assert-True -Condition ($topMoved -gt 0.05) `
        -Message "silhouette should displace the face at a glancing angle, but only $(Pct $topMoved) % of it changed"
}

Test 'silhouette is its own mode, distinct from both on and distance' {
    SendOk 'material_surface Bricks on 16 default' | Out-Null
    $on = Take 'mode-on'
    SendOk 'material_surface Bricks distance 16 default' | Out-Null
    $distance = Take 'mode-distance'
    SendOk 'material_surface Bricks silhouette 16 default' | Out-Null
    $silhouette = Take 'mode-silhouette'
    $fromOn = Differ $on $silhouette
    $fromDistance = Differ $distance $silhouette
    # Measured 14.9 % each; identical (0 %) before the view direction was fixed.
    Assert-True -Condition ($fromOn -gt 0.05) -Message "silhouette differs from on by only $(Pct $fromOn) % of the view"
    Assert-True -Condition ($fromDistance -gt 0.05) -Message "silhouette differs from distance by only $(Pct $fromDistance) % of the view"
}

Test 'the counter sees the tessellation it is asked about' {
    SendOk 'set_material box_a Bricks' | Out-Null
    Set-BoxView
    SendOk 'material_surface Bricks off 10 default' | Out-Null
    $off = Get-TessCount
    SendOk 'material_surface Bricks on 10 default' | Out-Null
    $on = Get-TessCount
    Assert-True -Condition ($on -gt $off + 100) `
        -Message "tessellating the cube at factor 10 should add vertices, but the count went from $off to $on"
    SendOk 'material_surface Bricks off 10 default' | Out-Null
    Assert-Equal -Expected $off -Actual (Get-TessCount) -Message 'and switching it off again returns to the same count'
}

Test 'on and distance tessellate identically up close, where the distance falloff has not begun' {
    # Factor 10, because every factor is rounded up to a power of two: at 10 the cube's edges
    # (8.7 for a side, 10.3 for the diagonal) sit well inside one step for both modes, where at
    # 16 the diagonal straddles 16 and a one-percent difference flips a whole subdivision.
    SendOk 'material_surface Bricks on 10 default' | Out-Null
    $on = Get-TessCount
    SendOk 'material_surface Bricks distance 10 default' | Out-Null
    $distance = Get-TessCount
    Assert-Equal -Expected $on -Actual $distance -Message 'at ~10 units distance is at full detail'
}

Test 'distance tessellates less than on far away' {
    # A 20-unit cube seen from ~69 units looks the size of the near view's, but the distance
    # falloff (1 - d^2/10000) has halved the factor, which crosses a power-of-two step.
    Set-BoxView -Scale 20 -Camera @(-20.0, 27.0, -60.0)
    SendOk 'material_surface Bricks on 4 default' | Out-Null
    $on = Get-TessCount
    SendOk 'material_surface Bricks distance 4 default' | Out-Null
    $distance = Get-TessCount
    Set-BoxView
    Assert-True -Condition ($distance -lt $on) `
        -Message "far away distance should have fewer vertices than on, but it has $distance against $on"
}

Test 'silhouette tessellates the face toward the camera less than on does' {
    SendOk 'material_surface Bricks on 10 default' | Out-Null
    $on = Get-TessCount
    SendOk 'material_surface Bricks silhouette 10 default' | Out-Null
    $silhouette = Get-TessCount
    SendOk 'material_surface Bricks off 10 default' | Out-Null
    $off = Get-TessCount
    Assert-True -Condition ($silhouette -lt $on) `
        -Message "silhouette should spend less than on at this angle, but has $silhouette against $on"
    Assert-True -Condition ($silhouette -gt $off) `
        -Message "silhouette still tessellates the faces at a glancing angle, but has $silhouette against $off with it off"
}

Test 'the two triangles of a face get the same tessellation: a 4 x 2 box counts like a 2 x 4 one' {
    # Turning a rectangle 90 degrees gives a rectangle - but the mesh's diagonal does not turn
    # with it, so a hull shader that treats the two triangles of a quad differently (which it
    # did: it scaled every edge by the triangle's height over its *first* edge, a side in one
    # triangle and the diagonal in the other) counts different totals for the two boxes, and
    # a symmetric one counts the same. The camera is the same, and so is the set of faces it
    # sees - front, top, left - just with their two extents swapped.
    SendOk 'material_surface Bricks on 6 default' | Out-Null
    Set-BoxView
    SendOk 'set_scale 4 2 3' | Out-Null
    Start-Sleep -Milliseconds 400
    Step-EditorFrames -Session $Session -Count 10
    $wide = Get-TessCount
    SendOk 'set_scale 2 4 3' | Out-Null
    Start-Sleep -Milliseconds 400
    Step-EditorFrames -Session $Session -Count 10
    $tall = Get-TessCount
    Set-BoxView
    Assert-Equal -Expected $wide -Actual $tall -Message 'the same rectangles in either order must tessellate alike'
}

Test 'a tessellated surface has no cracks: nothing shows through the faces' {
    # Where two triangles disagree about the factor on the edge they share, the surface is cut
    # at two different rates and a sliver of the background shows through along it. The top
    # face's interior reads 0 % near-black flat, and 1 - 1.5 % with the old hull shader.
    $top = @{ Left = 0.44; Top = 0.335; Right = 0.56; Bottom = 0.365 }
    SendOk 'material_surface Bricks off 0 default' | Out-Null
    $flat = Get-DarkShare -Path (Take 'cracks-off') @top
    Assert-True -Condition ($flat -lt 0.002) -Message "control: the flat face has $(Pct $flat) % near-black pixels in the sample - the framing moved"
    foreach ($mode in 'on', 'distance', 'silhouette') {
        SendOk "material_surface Bricks $mode 16 default" | Out-Null
        $cracked = Get-DarkShare -Path (Take "cracks-$mode") @top
        Assert-True -Condition ($cracked -lt 0.003) `
            -Message "mode $mode leaves $(Pct $cracked) % of the top face near-black - the surface has cracks (flat: $(Pct $flat) %)"
    }
}

# --- the relief follows the colour's UV mapping --------------------------------------
# The pixel shader remaps the UV (world-aligned tiling, or the UV scale) before it samples
# colour; the domain shader used to sample the height at the raw mesh UV, so a tiled
# material showed its bricks and its bumps at different frequencies. Both now go through
# MaterialUV. The tests use a fact about tiling: a 16x16 checker sampled at 2u is the same
# picture as that checker tiled 2x2 into 32x32 and sampled at u - colour AND relief, since
# each material uses the one image for both maps. If the relief ignores the mapping the pair
# disagrees; a control pair with a different tiling shows the render can tell.

Test 'a checker map and the same map tiled 2 x 2 import and become two materials' {
    Add-Type -AssemblyName System.Drawing
    $small = Join-Path $Assets 'materials\test_checker.png'
    $generated = Join-Path (Split-Path -Parent $ShotDir) 'generated'
    New-Item -ItemType Directory -Force $generated | Out-Null
    $src = New-Object System.Drawing.Bitmap $small
    $tiled = New-Object System.Drawing.Bitmap ($src.Width * 2), ($src.Height * 2)
    for ($y = 0; $y -lt $tiled.Height; $y++) {
        for ($x = 0; $x -lt $tiled.Width; $x++) {
            $tiled.SetPixel($x, $y, $src.GetPixel($x % $src.Width, $y % $src.Height))
        }
    }
    $tiledPath = Join-Path $generated 'checker_2x2.png'
    $tiled.Save($tiledPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $src.Dispose(); $tiled.Dispose()
    SendOk "import_texture $small Tile", "import_texture $tiledPath Tile" | Out-Null
    foreach ($material in @(@('TileSmall', 'Tile\test_checker.png'), @('TileBig', 'Tile\checker_2x2.png'))) {
        SendOk "create_material $($material[0]) materials\test.mat" | Out-Null
        SendOk "set_material_texture $($material[0]) diffuse $($material[1])",
               "set_material_texture $($material[0]) height $($material[1])",
               "material_surface $($material[0]) on 16 default" | Out-Null
    }
    Assert-Match -Pattern 'height=yes$' -Actual (Surface TileBig)
}

Test 'the relief follows the UV scale: the map at scale 2 displaces like the same map tiled twice at scale 1' {
    SendOk 'set_material_uv_scale TileSmall 2', 'set_material_uv_scale TileBig 1',
           'set_material_world_uv TileSmall 0 1', 'set_material_world_uv TileBig 0 1',
           'set_material box_a TileSmall' | Out-Null
    Set-BoxView
    $small = Take 'uvscale-small-x2'
    SendOk 'set_material box_a TileBig' | Out-Null
    $big = Take 'uvscale-big-x1'
    SendOk 'set_material_uv_scale TileSmall 1', 'set_material box_a TileSmall' | Out-Null
    $control = Take 'uvscale-small-x1'
    $matched = Differ $small $big
    $unmatched = Differ $control $big
    Assert-True -Condition ($unmatched -gt 0.05) `
        -Message "control: the same map at scale 1 should differ from the tiled one, but only $(Pct $unmatched) % of the view changed - the render cannot tell tilings apart"
    Assert-True -Condition ($matched -lt 0.02) `
        -Message "scale 2 and the 2 x 2 tiling should displace alike, but $(Pct $matched) % of the view differs (control: $(Pct $unmatched) %)"
}

Test 'the relief follows world-aligned tiling: 1.5 world units per repeat displaces like the tiled map at 3' {
    SendOk 'set_material_uv_scale TileSmall 1', 'set_material_uv_scale TileBig 1',
           'set_material_world_uv TileSmall 1 1.5', 'set_material_world_uv TileBig 1 3',
           'set_material box_a TileSmall' | Out-Null
    Set-BoxView
    $small = Take 'world-small-1.5'
    SendOk 'set_material box_a TileBig' | Out-Null
    $big = Take 'world-big-3'
    SendOk 'set_material_world_uv TileSmall 1 3', 'set_material box_a TileSmall' | Out-Null
    $control = Take 'world-small-3'
    $matched = Differ $small $big
    $unmatched = Differ $control $big
    Assert-True -Condition ($unmatched -gt 0.05) `
        -Message "control: 3 units per repeat of the small map should differ from the tiled one at 3, but only $(Pct $unmatched) % changed"
    Assert-True -Condition ($matched -lt 0.02) `
        -Message "1.5 units per repeat and the 2 x 2 tiling at 3 should displace alike, but $(Pct $matched) % of the view differs (control: $(Pct $unmatched) %)"
    SendOk 'set_material_world_uv TileSmall 0 1', 'set_material_world_uv TileBig 0 1' | Out-Null
}

# --- a second editor: what a load does with each mode and with the key ---------------

Test 'every mode survives a save and a reload, and a .mat without the key gets the default' {
    foreach ($mode in $modes.Keys) {
        SendOk "create_material Mode_$mode materials\test.mat" | Out-Null
        SendOk "material_surface Mode_$mode $mode 8 0.25" | Out-Null
    }
    SendOk 'material_surface Bricks silhouette 16 0.2' | Out-Null
    SendOk 'save_materials' | Out-Null
    $matPath = Join-Path $Assets 'materials\test.mat'
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $blue = $mat.materials | Where-Object { $_.name -eq 'TestBlue' }
    $noKey = $blue | Select-Object * -ExcludeProperty displacement_scale
    $noKey.name = 'NoKey'
    $mat.materials = @($mat.materials) + $noKey
    [IO.File]::WriteAllText($matPath, ($mat | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-tessellation'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $surface = { param($m) (Invoke-EditorCommand -Session $reloaded -Command "material_surface $m")[0].Text }
        foreach ($mode in $modes.Keys) {
            Assert-Equal -Expected "tess=$mode factor=8.000000 displacement=0.250000 height=no" -Actual (& $surface "Mode_$mode") `
                -Message "mode $mode came back"
        }
        Assert-Equal -Expected 'tess=silhouette factor=16.000000 displacement=0.200000 height=yes' -Actual (& $surface 'Bricks') `
            -Message 'the real texture set came back with its mode and every map'
        Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.100000 height=no' -Actual (& $surface 'NoKey') `
            -Message 'no displacement_scale key: the default'
        Assert-Equal -Expected 'tess=off factor=0.000000 displacement=0.000000 height=no' -Actual (& $surface 'TestRed') `
            -Message 'an explicit 0 stays off'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
