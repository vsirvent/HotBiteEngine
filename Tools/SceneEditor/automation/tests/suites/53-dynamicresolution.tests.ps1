# fixture: empty
# description: Dynamic resolution - the scene drawn at a size that follows the GPU load and reconstructed to the native picture by NVIDIA DLSS before anything that is not the scene. A per-level option stored with the render settings; it exists only where DLSS does, and the tests that need the upscale are skipped where it does not.

function Get-Drs { return (Get-Render -Session $Session) }

Test 'the render dump says what dynamic resolution is and whether the machine has it' {
    $r = Get-Drs
    foreach ($key in 'drs', 'drs_hz', 'drs_min_scale', 'drs_max_scale', 'drs_available', 'drs_active', 'drs_width', 'drs_height') {
        Assert-True -Condition ($null -ne $r.$key) -Message "the dump has $key"
    }
    Assert-Equal -Expected 'False' -Actual $r.drs -Message 'off in a level that does not ask for it'
    Assert-Equal -Expected 'False' -Actual $r.drs_active -Message 'and so not active'
    Assert-Near -Expected 60.0 -Actual $r.drs_hz -Tolerance 0.01 -Message 'the rate defaults to 60'
    Assert-Near -Expected 0.5 -Actual $r.drs_min_scale -Tolerance 0.01 -Message 'the smallest size to half'
    Assert-Near -Expected 1.0 -Actual $r.drs_max_scale -Tolerance 0.01 -Message 'the biggest to native'
}

Test 'the settings take only what makes sense' {
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render drs_hz 5')[0] -Message 'a rate under 24'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render drs_min_scale 0.1')[0] -Message 'a scale DLSS cannot take'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render drs_max_scale 1.5')[0] -Message 'a scale over native'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render drs maybe')[0] -Message 'not a switch'
    if (-not [bool](Get-Drs).drs_available) {
        Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render drs 1')[0] -Message 'no DLSS, no dynamic resolution'
        Assert-Equal -Expected 'False' -Actual (Get-Drs).drs -Message 'a refused request leaves it off'
    }
}

Test 'the options are stored in the level and come back, request and all' {
    SendOk 'render drs_hz 90', 'render drs_min_scale 0.6', 'render drs_max_scale 0.9' | Out-Null
    $available = [bool](Get-Drs).drs_available
    if ($available) { SendOk 'render drs 1' | Out-Null }
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $render = $level.editor.render
    foreach ($key in 'drs', 'drs_hz', 'drs_min_scale', 'drs_max_scale') {
        Assert-True -Condition ($null -ne $render.$key) -Message "the level's render block says $key"
    }
    Assert-Near -Expected 90.0 -Actual $render.drs_hz -Tolerance 0.01
    Assert-Near -Expected 0.6 -Actual $render.drs_min_scale -Tolerance 0.001
    Assert-Near -Expected 0.9 -Actual $render.drs_max_scale -Tolerance 0.001
    Assert-Equal -Expected ([string]$available) -Actual ([string]$render.drs) -Message 'on when it was asked for and could be'
    # What is not part of a level: pinning the scale, a debug aid.
    Assert-NotContains -Collection @($render.PSObject.Properties.Name) -Value 'drs_fixed_scale' -Message 'the pinned scale is never saved'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-drs'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Get-Render -Session $reloaded
        Assert-Near -Expected 90.0 -Actual $r.drs_hz -Tolerance 0.01 -Message 'reloaded'
        Assert-Near -Expected 0.6 -Actual $r.drs_min_scale -Tolerance 0.001
        Assert-Near -Expected 0.9 -Actual $r.drs_max_scale -Tolerance 0.001
        Assert-Equal -Expected ([string]$available) -Actual ([string]$r.drs) -Message 'and the switch'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
    SendOk 'render drs 0', 'render drs_hz 60', 'render drs_min_scale 0.5', 'render drs_max_scale 1' | Out-Null
}

Test 'the ray traced shadows blend is a level setting: validated, saved, and back on reload' {
    $r = Get-Drs
    Assert-Near -Expected 0.08 -Actual $r.shadow_blend -Tolerance 0.001 -Message 'the default is the engine own'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render shadow_blend 0')[0] -Message 'no weight at all would freeze the shadow'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render shadow_blend 2')[0] -Message 'more than the whole frame'
    Assert-Err -Result (Invoke-EditorCommand -Session $Session -Command 'render shadow_blend soft')[0] -Message 'not a number'
    SendOk 'render shadow_blend 0.4' | Out-Null
    Assert-Near -Expected 0.4 -Actual (Get-Drs).shadow_blend -Tolerance 0.001
    # The command that tunes it for tests sees the same value.
    Assert-True -Condition ((SendOk 'shadow_rays')[0].Text -match 'blend=0\.400') -Message 'shadow_rays reads the same blend'
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    Assert-Near -Expected 0.4 -Actual $level.editor.render.shadow_blend -Tolerance 0.001 -Message 'saved with the level'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-shadow-blend'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        Assert-Near -Expected 0.4 -Actual (Get-Render -Session $reloaded).shadow_blend -Tolerance 0.001 -Message 'reloaded'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
    SendOk 'render shadow_blend 0.08' | Out-Null
}

Test 'the scene is drawn smaller than the picture and DLSS makes the picture' {
    if (-not [bool](Get-Drs).drs_available) { Skip-Test -Reason 'no DLSS on this machine' }
    SendOk 'render drs_fixed_scale 0.6', 'render drs 1' | Out-Null
    $r = $null
    for ($i = 0; $i -lt 40; $i++) {
        Step-EditorFrames -Session $Session -Count 5
        $r = Get-Drs
        if ($r.drs_active -and $r.drs_upscaled_frames -ge 5 -and $r.drs_width -lt $r.drs_native_width) { break }
    }
    Assert-Equal -Expected 'True' -Actual ([string]$r.drs_active) -Message 'active'
    Assert-True -Condition ($r.drs_upscaled_frames -ge 5) -Message 'DLSS has upscaled frames'
    Assert-True -Condition ($r.drs_scale -ge 0.55 -and $r.drs_scale -le 0.7) -Message "at the scale that was pinned, not $($r.drs_scale)"
    Assert-True -Condition ($r.drs_width -lt $r.drs_native_width -and $r.drs_height -lt $r.drs_native_height) -Message 'a smaller picture was drawn'
    # Nothing in the editor draws wrong while it is up: a screenshot is taken and has the size of the window.
    $shot = Join-Path $ShotDir 'drs-scaled.png'
    SendOk "screenshot $shot" | Out-Null
    Assert-FileExists -Path $shot
    # Turned off, the picture is native again.
    SendOk 'render drs 0', 'render drs_fixed_scale 0' | Out-Null
    for ($i = 0; $i -lt 20; $i++) { Step-EditorFrames -Session $Session -Count 5; if (-not (Get-Drs).drs_active) { break } }
    $off = Get-Drs
    Assert-Equal -Expected 'False' -Actual ([string]$off.drs_active) -Message 'off again'
    Assert-Near -Expected 1.0 -Actual $off.drs_scale -Tolerance 0.001 -Message 'at the native size'
    Assert-Equal -Expected ([string]$off.drs_native_width) -Actual ([string]$off.drs_width) -Message 'the native width'
}
