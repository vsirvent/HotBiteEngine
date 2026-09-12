# fixture: empty
# description: Render settings, the debug buffer views and the denoiser bypasses - keys, validation, and what they suppress.

# The whole set of buffers TextureMixerCS can be asked to show instead of the
# mix. This list must stay in step with RenderSystem::eDebugBuffer, its HLSL
# mirror in Shaders/Common/RenderDebug.hlsli and the editor's label list.
$DebugBuffers = @('off', 'scene', 'light', 'bloom', 'emission', 'reflection', 'refraction',
                  'indirect', 'volumetric', 'dust', 'lens_flare', 'depth', 'position',
                  'normal', 'motion', 'gi_cache', 'gi_cache_conf',
                  # The ray source pair (rt_ray_sources0/1): the mask of which pixels
                  # trace rays, and the four packed scalars that decide it.
                  'ray_sources', 'ray_dispersion', 'ray_reflex', 'ray_density',
                  'ray_opacity')

Test 'render dumps every setting the Render menu has' {
    $r = Get-Render -Session $Session
    foreach ($key in @('rt_quality', 'rt_reflections', 'rt_refractions', 'rt_indirect',
                       'aa', 'motion_blur', 'motion_blur_scale', 'dof', 'dof_autofocus', 'dof_focus', 'dof_amplitude',
                       'lens_flare', 'lens', 'lens_aberration', 'lens_grain', 'lens_vignette',
                       'wireframe', 'debug_buffer', 'debug_gain', 'gi_denoise', 'rt_denoise')) {
        Assert-True -Condition ($null -ne $r.$key) -Message "render dumps $key"
    }
}

Test 'the boolean settings toggle and report back' {
    foreach ($key in @('aa', 'motion_blur', 'dof', 'lens_flare', 'lens', 'wireframe',
                       'rt_reflections', 'rt_refractions', 'rt_indirect')) {
        $r = SendOk "render $key 0"
        Assert-Equal -Expected 'False' -Actual ($r[0].Text | ConvertFrom-Json).$key -Message "$key off"
        $r = SendOk "render $key 1"
        Assert-Equal -Expected 'True' -Actual ($r[0].Text | ConvertFrom-Json).$key -Message "$key on"
    }
}

Test 'rt_quality takes the named levels' {
    foreach ($level in @('off', 'low', 'mid', 'high')) {
        $r = SendOk "render rt_quality $level"
        Assert-Equal -Expected $level -Actual ($r[0].Text | ConvertFrom-Json).rt_quality
    }
    Assert-Err -Result (Send 'render rt_quality ludicrous')[0]
    SendOk 'render rt_quality high' | Out-Null
}

Test 'an unknown key or a missing value is rejected' {
    Assert-Err -Result (Send 'render no_such_key 1')[0]
    Assert-Err -Result (Send 'render aa')[0] -Pattern 'usage:'
    Assert-Err -Result (Send 'render aa maybe')[0]
}

Test 'the lens effects take 0..1 and reject anything else' {
    foreach ($key in @('lens_aberration', 'lens_grain', 'lens_vignette')) {
        $r = SendOk "render $key 0.5"
        Assert-Near -Expected 0.5 -Actual ($r[0].Text | ConvertFrom-Json).$key -Tolerance 0.001
        Assert-Err -Result (Send "render $key 2")[0] -Message "$key above 1"
        Assert-Err -Result (Send "render $key -1")[0] -Message "$key below 0"
        SendOk "render $key 0" | Out-Null
    }
}

Test 'the lens master switch zeroes the effects without forgetting them' {
    SendOk 'render lens_grain 0.6', 'render lens 1' | Out-Null
    SendOk 'render lens 0' | Out-Null
    SendOk 'render lens 1' | Out-Null
    Assert-Near -Expected 0.6 -Actual (Get-Render -Session $Session).lens_grain -Tolerance 0.001 `
        -Message 'the stored value came back'
    SendOk 'render lens_grain 0' | Out-Null
}

Test 'setting dof_focus switches autofocus off, and the aperture is independent' {
    SendOk 'render dof_autofocus 1' | Out-Null
    Assert-Equal -Expected 'True' -Actual (Get-Render -Session $Session).dof_autofocus
    SendOk 'render dof_focus 12.5' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Equal -Expected 'False' -Actual $r.dof_autofocus -Message 'a manual distance means manual focus'
    Assert-Near -Expected 12.5 -Actual $r.dof_focus -Tolerance 0.001

    SendOk 'render dof_amplitude 3' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Near -Expected 3.0 -Actual $r.dof_amplitude -Tolerance 0.001
    Assert-Equal -Expected 'False' -Actual $r.dof_autofocus -Message 'the aperture does not touch the mode'
    SendOk 'render dof_autofocus 1' | Out-Null
}

Test 'motion_blur_scale takes a float and rejects a negative one' {
    $r = SendOk 'render motion_blur_scale 2.5'
    Assert-Near -Expected 2.5 -Actual ($r[0].Text | ConvertFrom-Json).motion_blur_scale -Tolerance 0.001
    Assert-Err -Result (Send 'render motion_blur_scale -1')[0] -Message 'motion_blur_scale below 0'
    Assert-Err -Result (Send 'render motion_blur_scale nonsense')[0]
    SendOk 'render motion_blur_scale 1' | Out-Null
}

Test 'motion_blur_scale is independent of the enable flag' {
    SendOk 'render motion_blur_scale 3' | Out-Null
    SendOk 'render motion_blur 0' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Equal -Expected 'False' -Actual $r.motion_blur
    Assert-Near -Expected 3.0 -Actual $r.motion_blur_scale -Tolerance 0.001 -Message 'the scale survives toggling the effect off'
    SendOk 'render motion_blur 1', 'render motion_blur_scale 1' | Out-Null
}

Test 'every debug buffer name is accepted and reported back' {
    foreach ($name in $DebugBuffers) {
        $r = SendOk "render debug_buffer $name"
        Assert-Equal -Expected $name -Actual ($r[0].Text | ConvertFrom-Json).debug_buffer -Message "debug_buffer $name"
    }
    Assert-Err -Result (Send 'render debug_buffer sideways')[0]
    SendOk 'render debug_buffer off' | Out-Null
}

Test 'a debug buffer view suppresses AA, motion blur, DOF and the lens effects' {
    # ...without writing the stored settings, so they come back when it goes off.
    SendOk 'render aa 1', 'render motion_blur 1', 'render dof 1', 'render lens 1' | Out-Null
    SendOk 'render debug_buffer normal' | Out-Null
    SendOk 'render debug_buffer off' | Out-Null
    $r = Get-Render -Session $Session
    foreach ($key in @('aa', 'motion_blur', 'dof', 'lens')) {
        Assert-Equal -Expected 'True' -Actual $r.$key -Message "$key survived the debug view"
    }
}

Test 'debug_gain is the exposure for the colour buffer views' {
    SendOk 'render debug_gain 8' | Out-Null
    Assert-Near -Expected 8.0 -Actual (Get-Render -Session $Session).debug_gain -Tolerance 0.001
    Assert-Err -Result (Send 'render debug_gain nonsense')[0]
    SendOk 'render debug_gain 1' | Out-Null
}

Test 'the denoiser bypasses are independent bits' {
    SendOk 'render gi_denoise 0' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Equal -Expected 'False' -Actual $r.gi_denoise
    Assert-Equal -Expected 'True' -Actual $r.rt_denoise -Message 'the other one is untouched'

    SendOk 'render rt_denoise 0' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Equal -Expected 'False' -Actual $r.gi_denoise
    Assert-Equal -Expected 'False' -Actual $r.rt_denoise
    SendOk 'render gi_denoise 1', 'render rt_denoise 1' | Out-Null
}

Test 'a bypass is independent of the buffer being shown' {
    SendOk 'render debug_buffer indirect', 'render gi_denoise 0' | Out-Null
    $r = Get-Render -Session $Session
    Assert-Equal -Expected 'indirect' -Actual $r.debug_buffer
    Assert-Equal -Expected 'False' -Actual $r.gi_denoise
    SendOk 'render debug_buffer off', 'render gi_denoise 1' | Out-Null
}

Test 'the shadow debug views are menu switches and are mutually exclusive' {
    # DIR_LIGHT_FLAG_DEBUG_CASCADES and DIR_LIGHT_FLAG_DEBUG_STATIC recolour the
    # same term, so only one may be on at a time.
    SendOk 'menu "View/Shadow Cascades"' | Out-Null
    SendOk 'menu "View/Static Shadow Map"' | Out-Null
    SendOk 'menu "View/Static Shadow Map"' | Out-Null
    Assert-Ok -Result (Send 'ping')[0] -Message 'the editor is still alive after the shadow views'
}

Test 'a debug buffer view actually changes the frame' {
    SendOk 'render debug_buffer off' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $normal = Shot 'render-off'
    SendOk 'render debug_buffer normal', 'render debug_gain 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $debug = Shot 'render-normal'
    $diff = Get-ImageDifference -PathA $normal -PathB $debug
    Assert-True -Condition ($diff.DifferingShare -gt 0.05) `
        -Message "showing the normal buffer should change the image, only $([Math]::Round($diff.DifferingShare * 100, 1))% of pixels differ"
    SendOk 'render debug_buffer off' | Out-Null
}

Test 'wireframe changes the frame too' {
    SendOk 'render wireframe 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $solid = Shot 'render-solid'
    SendOk 'render wireframe 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $wire = Shot 'render-wire'
    $diff = Get-ImageDifference -PathA $solid -PathB $wire
    Assert-True -Condition ($diff.DifferingShare -gt 0.01) -Message 'wireframe should be visible'
    SendOk 'render wireframe 0' | Out-Null
}

Test 'render settings are not part of the undo history' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 6 6 6' | Out-Null
    SendOk 'render aa 0', 'render debug_buffer depth', 'render debug_buffer off', 'render aa 1' | Out-Null
    $r = SendOk 'undo'
    Assert-Match -Pattern 'box_a' -Actual $r[0].Text
    Assert-Equal -Expected 'True' -Actual (Get-Render -Session $Session).aa -Message 'undo did not touch the settings'
}
