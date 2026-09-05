# fixture: empty
# description: Painting a multi-material mask by dragging a brush over the model in the viewport - MaskPaint::TryPaintAtScreenPoint (screen -> world ray via SelectionGizmo::ComputeMouseRay, then Core::RaycastMeshUV against the mesh's own triangles), the candidate-entity filter, and the interactive brush-mode toggle.

function GetLayer {
    param([string]$MultiMaterial, [int]$Index)
    $r = SendOk "layer $MultiMaterial $Index"
    return ($r[0].Payload[0] | ConvertFrom-Json)
}

# Scales box_a into a 6x1x6 slab at the origin and frames the camera on its top
# face - the same setup 19-multimaterials.tests.ps1's Set-SlabView uses, so a
# screen coordinate near the center of the frame reliably lands on the slab.
function Set-SlabView {
    SendOk 'select box_a', 'set_scale 6 1 6', 'set_position 0 0 0',
           'camera_pos -4 6 -9', 'camera_target 0 0 0' | Out-Null
    Start-Sleep -Milliseconds 400
    $want = @(-4.0, 6.0, -9.0)
    for ($i = 0; $i -lt 30; $i += 2) {
        $cam = Get-Camera -Session $Session
        $d = [Math]::Sqrt((($cam.world_position[0] - $want[0]) * ($cam.world_position[0] - $want[0])) +
                          (($cam.world_position[1] - $want[1]) * ($cam.world_position[1] - $want[1])) +
                          (($cam.world_position[2] - $want[2]) * ($cam.world_position[2] - $want[2])))
        if ($d -le 0.2) {
            Step-EditorFrames -Session $Session -Count 3
            return
        }
        Step-EditorFrames -Session $Session -Count 2
    }
    throw ("the camera never reached the slab view (at " +
           "$((Get-Camera -Session $Session).world_position -join ', '))")
}

# The screen's pixel dimensions, read back off a screenshot (nothing in `state`
# reports the resolution directly).
function Get-DisplaySize {
    $path = Shot 'display-probe'
    $bmp = New-Object System.Drawing.Bitmap($path)
    $size = [pscustomobject]@{ Width = $bmp.Width; Height = $bmp.Height }
    $bmp.Dispose()
    return $size
}

Test 'set up: box_a wears a multi-material, framed for the slab view' {
    # set_multi_material attaches a stack to a *material* (MultiMaterialOps::Assign) -
    # box_a wears it by wearing TestRed, the material the stack is attached to.
    SendOk 'create_multi_material MMPaint materials\test.mat' | Out-Null
    SendOk 'add_layer MMPaint TestRed' | Out-Null
    SendOk 'set_material box_a TestRed' | Out-Null
    SendOk 'set_multi_material TestRed MMPaint' | Out-Null
    Set-SlabView
}

Test 'paint_stroke_screen paints at the surface under the cursor' {
    $display = Get-DisplaySize
    $cx = $display.Width / 2.0
    $cy = $display.Height / 2.0

    SendOk 'paint_mask_begin MMPaint 0 64' | Out-Null
    # A short "drag": several dabs tracing across the center of the slab.
    for ($i = -2; $i -le 2; $i++) {
        SendOk "paint_stroke_screen $($cx + $i * 8) $($cy + $i * 4) 0.3 1" | Out-Null
    }
    $layer = GetLayer -MultiMaterial 'MMPaint' -Index 0
    Assert-True -Condition (($layer.flags -band 512) -ne 0) -Message 'TEXT_MASK set once a live mask is bound'

    $maskPath = Join-Path $Assets 'masks\MMPaint_layer0_mask.png'
    SendOk 'paint_mask_commit' | Out-Null
    Assert-FileExists -Path $maskPath -Message 'the stroke was written to disk'
}

Test 'a screen point over empty sky paints nothing' {
    SendOk 'paint_mask_begin MMPaint 0 64' | Out-Null
    Assert-Err -Result (Send 'paint_stroke_screen 2 2 0.3 1')[0] -Pattern 'no surface'
    SendOk 'paint_mask_cancel' | Out-Null
}

Test 'a surface not wearing this multi-material is not painted' {
    # Reassign box_a to TestBlue (no multi-material attached): the same screen
    # point that hit it a moment ago now hits a plain surface instead, which the
    # candidate-entity filter must reject even though there is clearly a surface
    # under the cursor.
    SendOk 'set_material box_a TestBlue' | Out-Null
    $display = Get-DisplaySize
    $cx = $display.Width / 2.0
    $cy = $display.Height / 2.0

    SendOk 'paint_mask_begin MMPaint 0 64' | Out-Null
    Assert-Err -Result (Send "paint_stroke_screen $cx $cy 0.3 1")[0] -Pattern 'no entity in the scene uses'
    SendOk 'paint_mask_cancel' | Out-Null

    SendOk 'set_material box_a TestRed' | Out-Null
}

Test 'paint_stroke_screen requires an open session' {
    Assert-Err -Result (Send 'paint_stroke_screen 100 100 0.3 1')[0] -Pattern 'no paint session'
}

Test 'paint_stroke_screen validates its arguments' {
    SendOk 'paint_mask_begin MMPaint 0 64' | Out-Null
    Assert-Err -Result (Send 'paint_stroke_screen 100 100')[0] -Pattern 'usage:'
    Assert-Err -Result (Send 'paint_stroke_screen a b c d')[0] -Pattern 'usage:'
    SendOk 'paint_mask_cancel' | Out-Null
}

Test 'set_mask_paint_brush_mode toggles and is echoed by state' {
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).mask_paint_brush_mode
    SendOk 'set_mask_paint_brush_mode 1' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-State -Session $Session).mask_paint_brush_mode
    SendOk 'set_mask_paint_brush_mode 0' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).mask_paint_brush_mode
}

Test 'committing a session turns brush mode back off' {
    SendOk 'set_mask_paint_brush_mode 1' | Out-Null
    SendOk 'paint_mask_begin MMPaint 0 64' | Out-Null
    SendOk 'paint_mask_commit' | Out-Null
    Assert-Equal -Expected 'False' -Actual (Get-State -Session $Session).mask_paint_brush_mode `
        -Message 'a checkbox tied to a session should not survive it'
}
