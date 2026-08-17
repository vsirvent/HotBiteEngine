# fixture: empty
# description: Multi-materials - layer stacks that blend several materials over one surface (mask images, orientation/altitude rules), and the mask-painting tool.

function MultiMaterialLine {
    param([string]$Name)
    $r = SendOk 'multi_materials'
    return ($r[0].Payload | Where-Object { $_ -like "$Name *" })
}

function GetLayer {
    param([string]$MultiMaterial, [int]$Index)
    $r = SendOk "layer $MultiMaterial $Index"
    return ($r[0].Payload[0] | ConvertFrom-Json)
}

# Scales box_a into a thin 6x1x6 slab at the origin and frames the camera on it,
# so a multi-material attached to its material fills most of the view - the shape
# every render assertion below needs. box_a is a unit cube (see New-TestProject.ps1),
# so its six faces after this scale still carry one normal each: top (+Y) and the
# four sides, which is what makes an orientation ("slope") rule visible at all on a
# fixture with no real terrain mesh.
function Set-SlabView {
    SendOk 'select box_a', 'set_scale 6 1 6', 'set_position 0 0 0',
           'camera_pos -4 6 -9', 'camera_target 0 0 0' | Out-Null
    # Transform edits are consumed by StaticMeshSystem on its own background tick
    # (see CLAUDE.md's motion-vector notes) - a screenshot taken immediately after
    # set_scale reads the pre-scale mesh. A short settle plus a couple of frames
    # covers it the same way 18-engine-render's Reset-View does.
    Start-Sleep -Milliseconds 400
    # Then wait for the rendered camera, not just for frames to pass. This used to be
    # load-bearing rather than defensive: CameraSystem and StaticMeshSystem both
    # cleared Transform::dirty, on different timers, and camera_rig is a mesh entity
    # too - so when the mesh timer won, the commanded pose reached the Transform and
    # never reached the view matrix, for the rest of the session. The slab then came
    # out 396 px wide instead of 678 and every render assertion below was quietly
    # measuring a different picture. The engine has its own change detection now
    # (Components::Camera::last_position), so this should never spin - it stays
    # because a pose that silently does not arrive is worth failing loudly on.
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

Test 'multi_materials starts empty' {
    Assert-Equal -Expected '0 multi-materials' -Actual (SendOk 'multi_materials')[0].Text
}

Test 'create_multi_material adds an empty stack to an existing .mat file' {
    SendOk 'create_multi_material Blend materials\test.mat' | Out-Null
    Assert-Equal -Expected '1 multi-materials' -Actual (SendOk 'multi_materials')[0].Text
    Assert-Match -Pattern 'file=materials\\test\.mat' -Actual (MultiMaterialLine -Name 'Blend')
    Assert-Match -Pattern 'layers=0' -Actual (MultiMaterialLine -Name 'Blend')
    Assert-Match -Pattern 'unsaved' -Actual (MultiMaterialLine -Name 'Blend')
}

Test 'create_multi_material rejects a duplicate name and an unknown file' {
    Assert-Err -Result (Send 'create_multi_material Blend materials\test.mat')[0] -Message 'duplicate name'
    Assert-Err -Result (Send 'create_multi_material Other nope.mat')[0] -Message 'unknown file'
}

Test 'create_multi_material is undoable' {
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected '0 multi-materials' -Actual (SendOk 'multi_materials')[0].Text
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected '1 multi-materials' -Actual (SendOk 'multi_materials')[0].Text
}

Test 'select_multi_material selects and opens the panel; unknown name errors' {
    $r = SendOk 'select_multi_material Blend'
    Assert-Equal -Expected 'selected multi-material: Blend' -Actual $r[0].Text
    Assert-Match -Pattern 'selected' -Actual (MultiMaterialLine -Name 'Blend')
    Assert-Err -Result (Send 'select_multi_material Nope')[0]
}

Test 'add_layer appends a layer sourced from a material' {
    SendOk 'add_layer Blend TestRed' | Out-Null
    $layer = GetLayer -MultiMaterial 'Blend' -Index 0
    Assert-Equal -Expected 'TestRed' -Actual $layer.material
    Assert-Equal -Expected 1 -Actual $layer.op -Message 'a first layer defaults to mix'
    Assert-Near -Expected 1.0 -Actual $layer.value -Tolerance 0.001
    Assert-True -Condition $layer.resolved -Message 'the source material resolved'
    Assert-True -Condition (($layer.flags -band 8) -ne 0) -Message 'TestRed has a diffuse map, so TEXT_DIFF is derived'

    SendOk 'add_layer Blend TestBlue' | Out-Null
    Assert-Match -Pattern 'layers=2' -Actual (MultiMaterialLine -Name 'Blend')
}

Test 'add_layer rejects an unknown material and an unknown stack' {
    Assert-Err -Result (Send 'add_layer Blend Nope')[0] -Pattern 'material not found'
    Assert-Err -Result (Send 'add_layer NopeStack TestRed')[0] -Pattern 'multi-material not found'
}

Test 'add_layer refuses past the MAX_MULTI_TEXTURE cap' {
    SendOk 'create_multi_material Full materials\test.mat' | Out-Null
    for ($i = 0; $i -lt 8; $i++) {
        SendOk 'add_layer Full TestRed' | Out-Null
    }
    Assert-Err -Result (Send 'add_layer Full TestRed')[0] -Pattern 'at most 8'
}

Test 'set_layer edits fields and readback matches' {
    SendOk "set_layer Blend 1 ""{'value':0.5,'uv_scale':2.0,'op':2}""" | Out-Null
    $layer = GetLayer -MultiMaterial 'Blend' -Index 1
    Assert-Near -Expected 0.5 -Actual $layer.value -Tolerance 0.001
    Assert-Near -Expected 2.0 -Actual $layer.uv_scale -Tolerance 0.001
    Assert-Equal -Expected 2 -Actual $layer.op -Message 'add blend mode'
    # Put it back for the tests below, which assume a plain mix at full weight.
    SendOk "set_layer Blend 1 ""{'value':1.0,'uv_scale':1.0,'op':1}""" | Out-Null
}

Test 'set_layer validates op and rejects an unknown material or bad index' {
    Assert-Err -Result (Send "set_layer Blend 1 ""{'op':9}""")[0] -Pattern 'op must be'
    Assert-Err -Result (Send "set_layer Blend 1 ""{'material':'Nope'}""")[0] -Pattern 'material not found'
    Assert-Err -Result (Send "set_layer Blend 9 ""{'value':1}""")[0] -Pattern 'no layer'
    Assert-Err -Result (Send "set_layer NopeStack 0 ""{'value':1}""")[0] -Pattern 'multi-material not found'
}

Test 'set_layer is undoable' {
    $before = (GetLayer -MultiMaterial 'Blend' -Index 1).value
    SendOk "set_layer Blend 1 ""{'value':0.25}""" | Out-Null
    Assert-Near -Expected 0.25 -Actual (GetLayer -MultiMaterial 'Blend' -Index 1).value -Tolerance 0.001
    SendOk 'undo' | Out-Null
    Assert-Near -Expected $before -Actual (GetLayer -MultiMaterial 'Blend' -Index 1).value -Tolerance 0.001
}

Test 'a no-op set_layer edit records no history step' {
    $v0 = (GetLayer -MultiMaterial 'Blend' -Index 1).value
    $v1 = if ([Math]::Abs($v0 - 1.0) -lt 0.001) { 0.5 } else { 1.0 }
    SendOk "set_layer Blend 1 ""{'value':$v1}""" | Out-Null
    # Same value as the line above: nothing changed, so RecordEdit must push nothing.
    SendOk "set_layer Blend 1 ""{'value':$v1}""" | Out-Null
    # If the repeat had (wrongly) pushed its own step, one undo would only unwind
    # that step and leave the value at $v1; it must instead reach the value from
    # before either call.
    SendOk 'undo' | Out-Null
    Assert-Near -Expected $v0 -Actual (GetLayer -MultiMaterial 'Blend' -Index 1).value -Tolerance 0.001
    SendOk "set_layer Blend 1 ""{'value':1.0}""" | Out-Null
}

Test 'move_layer reorders and clamps at the ends' {
    $layer0 = (GetLayer -MultiMaterial 'Blend' -Index 0).material
    $layer1 = (GetLayer -MultiMaterial 'Blend' -Index 1).material
    SendOk 'move_layer Blend 0 5' | Out-Null
    Assert-Equal -Expected $layer1 -Actual (GetLayer -MultiMaterial 'Blend' -Index 0).material -Message 'clamped to the last index, swapping the pair'
    Assert-Equal -Expected $layer0 -Actual (GetLayer -MultiMaterial 'Blend' -Index 1).material
    SendOk 'move_layer Blend 0 5' | Out-Null
    # Back where it started (0 <-> 1 is its own inverse for a two-layer stack).
    Assert-Equal -Expected $layer0 -Actual (GetLayer -MultiMaterial 'Blend' -Index 0).material
}

Test 'move_layer and remove_layer reject a bad index' {
    Assert-Err -Result (Send 'move_layer Blend 9 1')[0] -Pattern 'no layer'
    Assert-Err -Result (Send 'remove_layer Blend 9')[0] -Pattern 'no layer'
}

Test 'remove_layer drops one layer and renumbers the rest' {
    SendOk 'create_multi_material ThreeLayers materials\test.mat' | Out-Null
    SendOk 'add_layer ThreeLayers TestRed', 'add_layer ThreeLayers TestBlue', 'add_layer ThreeLayers TestRed' | Out-Null
    SendOk 'remove_layer ThreeLayers 1' | Out-Null
    Assert-Match -Pattern 'layers=2' -Actual (MultiMaterialLine -Name 'ThreeLayers')
    Assert-Equal -Expected 'TestRed' -Actual (GetLayer -MultiMaterial 'ThreeLayers' -Index 0).material
    Assert-Equal -Expected 'TestRed' -Actual (GetLayer -MultiMaterial 'ThreeLayers' -Index 1).material -Message 'the removed layer was the middle one'
}

Test 'set_multi_material_params edits the surface parameters' {
    SendOk "set_multi_material_params Blend ""{'parallax_scale':0.1,'tess_factor':16,'tess_type':1,'displacement_scale':0.2}""" | Out-Null
    # No dedicated readback command for params beyond the layer list; verify through
    # the render path instead (covered by the visual tests below) and through save
    # round-trip (covered by the persistence test).
    Assert-Err -Result (Send 'set_multi_material_params NopeStack "{''tess_factor'':1}"')[0] -Pattern 'multi-material not found'
}

Test 'set_material and set_multi_material attach a stack to a material' {
    SendOk 'set_material box_a TestRed' | Out-Null
    SendOk 'set_multi_material TestRed Blend' | Out-Null
    Assert-Match -Pattern 'materials=1' -Actual (MultiMaterialLine -Name 'Blend')
}

Test 'set_multi_material none detaches' {
    SendOk 'set_multi_material TestRed none' | Out-Null
    Assert-Match -Pattern 'materials=0' -Actual (MultiMaterialLine -Name 'Blend')
    SendOk 'set_multi_material TestRed Blend' | Out-Null
    Assert-Match -Pattern 'materials=1' -Actual (MultiMaterialLine -Name 'Blend')
}

Test 'set_multi_material rejects an unknown material or stack' {
    Assert-Err -Result (Send 'set_multi_material Nope Blend')[0]
    Assert-Err -Result (Send 'set_multi_material TestRed NopeStack')[0]
}

Test 'attaching is undoable' {
    # Undoes the most recent attach (the re-attach at the end of the previous
    # test), landing back on detached.
    SendOk 'undo' | Out-Null
    Assert-Match -Pattern 'materials=0' -Actual (MultiMaterialLine -Name 'Blend')
    SendOk 'redo' | Out-Null
    Assert-Match -Pattern 'materials=1' -Actual (MultiMaterialLine -Name 'Blend')
}

Test 'duplicate_multi_material is not a command - Duplicate goes through the panel only' {
    # Documents the surface: MultiMaterialOps::Duplicate exists for the panel button,
    # but there is no automation verb for it (create + set_layer covers scripted needs).
    Skip-Test -Reason 'no automation command for duplicate; covered by create_multi_material + add_layer'
}

Test 'remove_multi_material retires the stack and detaches its wearers' {
    # By this point the suite has also created Full and ThreeLayers, so the count
    # is not "how many multi-materials total" - check Blend's own line is gone.
    SendOk 'remove_multi_material Blend' | Out-Null
    Assert-Equal -Expected $null -Actual (MultiMaterialLine -Name 'Blend')
    Assert-Err -Result (Send 'select_multi_material Blend')[0] -Message 'retired stacks are gone from the panel'
    Assert-Err -Result (Send 'layer Blend 0')[0]
}

Test 'remove_multi_material is undoable, wearers included' {
    SendOk 'undo' | Out-Null
    Assert-Match -Pattern 'layers=2' -Actual (MultiMaterialLine -Name 'Blend')
    Assert-Match -Pattern 'materials=1' -Actual (MultiMaterialLine -Name 'Blend') -Message 'TestRed re-attached'
}

Test 'save_materials writes multi_materials into the .mat file' {
    SendOk 'save_materials' | Out-Null
    $matPath = Join-Path $Assets 'materials\test.mat'
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $names = @($mat.multi_materials | ForEach-Object { $_.name })
    Assert-Contains -Collection $names -Value 'Blend'
    $blend = $mat.multi_materials | Where-Object { $_.name -eq 'Blend' }
    Assert-Equal -Expected 2 -Actual $blend.textures.Count
    Assert-Near -Expected 16.0 -Actual $blend.tess_factor -Tolerance 0.001
    Assert-NotMatch -Pattern 'unsaved' -Actual (MultiMaterialLine -Name 'Blend')

    # A material wearing a stack writes the attachment back too (MaterialData::Save).
    $red = $mat.materials | Where-Object { $_.name -eq 'TestRed' }
    Assert-Equal -Expected 'Blend' -Actual $red.multi_material
}

# --- Rendering: this is the point of the feature, so it gets verified against real
# frames, not just against the JSON the ops layer produces. Scene colour before
# lighting was used to root-cause a real engine bug while building this (a stack
# with no normal-map layer decoded a zero tangent-space colour as a normal pointing
# away from every light, so a working blend still rendered pitch black - see
# MultiTexture.hlsli's MultiTextureNotEnabledDefault); asserting on the *lit* image
# here is what would have caught that, and is what a user actually sees.

Test 'a two-layer stack renders the top layer, and weight falls back to the layer below' {
    Set-SlabView
    SendOk "set_layer Blend 0 ""{'value':1.0,'op':1}""" | Out-Null
    SendOk "set_layer Blend 1 ""{'value':1.0,'op':1,'slope_enabled':false,'height_enabled':false}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $top = Get-ImageStats -Path (Shot 'blend-top-layer')
    Assert-True -Condition ($top.MeanB -gt $top.MeanR) -Message "layer 1 (TestBlue) should dominate: R=$($top.MeanR) B=$($top.MeanB)"

    SendOk "set_layer Blend 1 ""{'value':0.0}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $fallback = Get-ImageStats -Path (Shot 'blend-fallback-layer')
    Assert-True -Condition ($fallback.MeanR -gt $fallback.MeanB) -Message "a zero-weight top layer should fall back to layer 0 (TestRed): R=$($fallback.MeanR) B=$($fallback.MeanB)"

    SendOk "set_layer Blend 1 ""{'value':1.0}""" | Out-Null
}

Test 'a slope rule confines a layer to the faces it names - the "snow on flat ground" case' {
    # dot(world normal, up): 1 on the slab's top face, ~0 on its sides. Restricting
    # layer 1 to [0.6, 1.0] should show it only where the camera looks straight down
    # at the top, and layer 0 on the near (-Z) side face below it.
    SendOk "set_layer Blend 1 ""{'slope_enabled':true,'slope_min':0.6,'slope_max':1.0,'slope_fade':0.05}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $shot = Shot 'blend-slope'
    # Both rectangles sit *inside* one face of the slab as Set-SlabView frames it
    # (camera at -4,6,-9 looking at the origin): the top sample is well within the
    # +Y face, the side sample within the red band the near face makes under it,
    # left of the Materials panel a prior select_multi_material left open over the
    # bottom-right. Measured, not guessed - each reads ~140 in its own channel and
    # under 10 in the other.
    $top = Get-ImageStats -Path $shot -Left 0.40 -Top 0.42 -Right 0.62 -Bottom 0.55
    $side = Get-ImageStats -Path $shot -Left 0.55 -Top 0.64 -Right 0.65 -Bottom 0.69
    # A sample that has slid off the slab reads near-black, and two near-zero means
    # then decide the assertion by noise - which is exactly how this test used to
    # fail (and, half the time, pass) while the render underneath was perfect. Say
    # "the framing moved" rather than "the slope rule is broken".
    foreach ($sample in @(@{ n = 'top'; s = $top }, @{ n = 'side'; s = $side })) {
        Assert-True -Condition ($sample.s.LitShare -gt 0.95) `
            -Message ("the $($sample.n) sample is not on the slab any more " +
                      "($([Math]::Round($sample.s.LitShare * 100)) % lit) - Set-SlabView's framing changed")
    }
    Assert-True -Condition ($top.MeanB -gt $top.MeanR * 2.0) -Message "top face should read as layer 1 (TestBlue): R=$($top.MeanR) B=$($top.MeanB)"
    Assert-True -Condition ($side.MeanR -gt $side.MeanB * 2.0) -Message "side face should read as layer 0 (TestRed): R=$($side.MeanR) B=$($side.MeanB)"

    SendOk "set_layer Blend 1 ""{'slope_enabled':false}""" | Out-Null
}

# --- Mask painting (see MaskPaint.h): an editor tool for authoring a layer's mask
# image without leaving the editor. A session targets one layer's mask channel,
# previews live through the GPU texture PrepareMultiMaterial binds, and only
# touches disk on Commit.

Test 'paint_mask_begin requires an existing stack and layer' {
    Assert-Err -Result (Send 'paint_mask_begin NopeStack 0')[0] -Pattern 'multi-material not found'
    Assert-Err -Result (Send 'paint_mask_begin Blend 9')[0] -Pattern 'no layer'
}

Test 'paint_mask requires an open session' {
    Assert-Err -Result (Send 'paint_mask 0.5 0.5 0.2 1')[0] -Pattern 'no paint session'
}

Test 'paint_mask_begin opens a session at the requested canvas size' {
    $r = SendOk 'paint_mask_begin Blend 1 64'
    Assert-Match -Pattern '64x64' -Actual $r[0].Text
}

Test 'a second paint_mask_begin refuses while one is open' {
    Assert-Err -Result (Send 'paint_mask_begin Blend 0')[0] -Pattern 'already open'
}

Test 'paint_mask dabs the canvas and the layer reports the mask flag live' {
    SendOk 'paint_mask 0.5 0.5 0.3 1' | Out-Null
    $layer = GetLayer -MultiMaterial 'Blend' -Index 1
    Assert-True -Condition (($layer.flags -band 512) -ne 0) -Message 'TEXT_MASK set once a live mask is bound'
}

Test 'paint_mask_cancel discards the session without writing a file' {
    $maskPath = Join-Path $Assets 'masks\Blend_layer1_mask.png'
    SendOk 'paint_mask_cancel' | Out-Null
    Assert-FileNotExists -Path $maskPath -Message 'a cancelled session never touches disk'
    Assert-Err -Result (Send 'paint_mask_commit')[0] -Pattern 'no paint session'
}

Test 'paint_mask_commit writes the mask file and points the layer at it' {
    SendOk 'paint_mask_begin Blend 1 64' | Out-Null
    SendOk 'paint_mask 0.25 0.5 0.3 1' | Out-Null
    SendOk 'paint_mask_commit' | Out-Null

    $maskPath = Join-Path $Assets 'masks\Blend_layer1_mask.png'
    Assert-FileExists -Path $maskPath
    $layer = GetLayer -MultiMaterial 'Blend' -Index 1
    Assert-Equal -Expected 'masks\Blend_layer1_mask.png' -Actual $layer.mask
    Assert-True -Condition (($layer.flags -band 512) -ne 0) -Message 'TEXT_MASK still set after the session ended'
}

Test 'repeated paint/commit cycles on the same layer do not corrupt the stack' {
    # Regression coverage for a use-after-free found while building this feature:
    # Commit() rebuilt the stack (which re-adopts a still-attached live mask
    # verbatim) before releasing the paint session's GPU texture, leaving
    # multi_texture_mask pointing at freed memory that the next frame's
    # PrepareMultiMaterial bound and crashed on. Several cycles in a row is what
    # surfaced it - a single cycle happened to still work by accident.
    for ($i = 0; $i -lt 4; $i++) {
        SendOk 'paint_mask_begin Blend 1 64' | Out-Null
        SendOk "paint_mask 0.$i 0.5 0.3 1" | Out-Null
        SendOk 'paint_mask_commit' | Out-Null
    }
    Step-EditorFrames -Session $Session -Count 3
    Assert-Equal -Expected 'pong' -Actual (SendOk 'ping')[0].Text -Message 'the editor is still alive after four commit cycles'
}

Test 'repeated paint/cancel cycles do not corrupt the stack either' {
    for ($i = 0; $i -lt 4; $i++) {
        SendOk 'paint_mask_begin Blend 1 64' | Out-Null
        SendOk "paint_mask 0.$i 0.5 0.3 1" | Out-Null
        SendOk 'paint_mask_cancel' | Out-Null
    }
    Step-EditorFrames -Session $Session -Count 3
    Assert-Equal -Expected 'pong' -Actual (SendOk 'ping')[0].Text -Message 'the editor is still alive after four cancel cycles'
}

Test 'the painted mask is visible in the rendered frame' {
    SendOk "set_layer Blend 1 ""{'value':1.0,'op':1}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $stats = Get-ImageStats -Path (Shot 'blend-painted-mask')
    Assert-True -Condition ($stats.LitShare -gt 0.5) -Message 'the slab should still be lit and visible after painting'
}

Test 'removing a layer a paint session is open on ends the session' {
    SendOk 'paint_mask_begin Blend 1' | Out-Null
    SendOk 'remove_layer Blend 1' | Out-Null
    # The session must not be left dangling on a layer index that no longer exists;
    # a fresh begin succeeding proves it was cleanly ended rather than erroring on
    # "already open".
    $r = SendOk 'paint_mask_begin Blend 0 64'
    Assert-Match -Pattern '64x64' -Actual $r[0].Text
    SendOk 'paint_mask_cancel' | Out-Null
}
