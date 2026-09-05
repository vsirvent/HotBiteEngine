# fixture: empty
# description: World-aligned texture tiling (Material.h's WORLD_UV_ENABLED_FLAG, MainRenderPS.hlsli) - the material property round trip, undo, and that it visibly changes the tiling frequency on a scaled surface.

# Scales box_a into a 6x1x6 slab at the origin and frames the camera on its top
# face - the exact setup 19-multimaterials.tests.ps1's Set-SlabView uses (box_a
# is a unit cube, so scaling it this way gives one big flat face to see a
# tiling pattern on, per that file's own comment).
function Set-BoxView {
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
    throw ("the camera never reached the box view (at " +
           "$((Get-Camera -Session $Session).world_position -join ', '))")
}

Test 'set_material_world_uv edits the flag and scale, and both are saved to the .mat file' {
    $matPath = Join-Path $Assets 'materials\test.mat'
    SendOk 'set_material_world_uv TestChecker 1 2.5' | Out-Null
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $rec = $mat.materials | Where-Object { $_.name -eq 'TestChecker' }
    Assert-True -Condition ($null -ne $rec) -Message 'TestChecker is in the file'
    Assert-Equal -Expected 'True' -Actual $rec.world_uv_enabled
    Assert-Near -Expected 2.5 -Actual $rec.world_uv_scale

    SendOk 'set_material_world_uv TestChecker 0 1' | Out-Null
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $rec = $mat.materials | Where-Object { $_.name -eq 'TestChecker' }
    Assert-Equal -Expected 'False' -Actual $rec.world_uv_enabled
    Assert-Near -Expected 1.0 -Actual $rec.world_uv_scale
}

Test 'set_material_world_uv validates its arguments' {
    Assert-Err -Result (Send 'set_material_world_uv TestChecker')[0] -Pattern 'usage:'
    Assert-Err -Result (Send 'set_material_world_uv NoSuchMaterial 1 2')[0] -Pattern 'not found'
    Assert-Err -Result (Send 'set_material_world_uv TestChecker 1 not-a-number')[0]
}

Test 'set_material_world_uv is undoable' {
    SendOk 'set_material_world_uv TestChecker 1 3' | Out-Null
    SendOk 'undo' | Out-Null
    $matPath = Join-Path $Assets 'materials\test.mat'
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $rec = $mat.materials | Where-Object { $_.name -eq 'TestChecker' }
    Assert-Equal -Expected 'False' -Actual $rec.world_uv_enabled -Message 'undo restored the flag'
    Assert-Near -Expected 1.0 -Actual $rec.world_uv_scale -Message 'undo restored the scale'
}

Test 'world-aligned tiling visibly changes the checker frequency on a scaled surface' {
    SendOk 'set_material box_a TestChecker' | Out-Null
    Set-BoxView

    SendOk 'set_material_world_uv TestChecker 0 1' | Out-Null
    $shotOff = Join-Path $ShotDir 'worlduv_off.png'
    SendOk "screenshot $shotOff" | Out-Null

    SendOk 'set_material_world_uv TestChecker 1 1' | Out-Null
    $shotOn = Join-Path $ShotDir 'worlduv_on.png'
    SendOk "screenshot $shotOn" | Out-Null

    $diff = Get-ImageDifference -PathA $shotOff -PathB $shotOn
    Assert-True -Condition ($diff.DifferingShare -gt 0.1) `
        -Message "expected the tiling to visibly change (differing=$($diff.DifferingShare), mean=$($diff.MeanDelta))"

    SendOk 'set_material_world_uv TestChecker 0 1' | Out-Null
}

Test 'world_uv_scale changes the tile size, not just whether tiling is on' {
    SendOk 'set_material box_a TestChecker' | Out-Null
    Set-BoxView

    SendOk 'set_material_world_uv TestChecker 1 1' | Out-Null
    $shotA = Join-Path $ShotDir 'worlduv_scale1.png'
    SendOk "screenshot $shotA" | Out-Null

    SendOk 'set_material_world_uv TestChecker 1 3' | Out-Null
    $shotB = Join-Path $ShotDir 'worlduv_scale3.png'
    SendOk "screenshot $shotB" | Out-Null

    $diff = Get-ImageDifference -PathA $shotA -PathB $shotB
    Assert-True -Condition ($diff.DifferingShare -gt 0.1) `
        -Message "expected a different tile size to render differently (differing=$($diff.DifferingShare), mean=$($diff.MeanDelta))"

    SendOk 'set_material_world_uv TestChecker 0 1' | Out-Null
}
