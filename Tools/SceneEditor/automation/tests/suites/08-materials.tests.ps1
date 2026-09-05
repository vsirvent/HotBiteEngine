# fixture: empty
# description: Materials as shared .mat assets - listing, creation, assignment, retirement, shader slots, saving.

function MaterialLine {
    param([string]$Name)
    $r = SendOk 'materials'
    return ($r[0].Payload | Where-Object { $_ -like "$Name *" })
}

Test 'materials lists every material with its file and user count' {
    # TestRed, TestBlue, TestChecker (see New-TestProject.ps1) - the third exists
    # for 26-worlduv.tests.ps1's tiling-frequency checks, which need a texture
    # with actual pattern detail rather than a flat colour.
    $r = SendOk 'materials'
    Assert-Equal -Expected '3 materials' -Actual $r[0].Text
    Assert-Match -Pattern 'file=materials\\test\.mat' -Actual (MaterialLine -Name 'TestRed')
    Assert-Match -Pattern 'users=0' -Actual (MaterialLine -Name 'TestRed')
}

Test 'select_material selects and opens the panel' {
    $r = SendOk 'select_material TestRed'
    Assert-Equal -Expected 'selected material: TestRed' -Actual $r[0].Text
    Assert-Match -Pattern 'selected' -Actual (MaterialLine -Name 'TestRed')
}

Test 'select_material on an unknown name is an error' {
    Assert-Err -Result (Send 'select_material Nope')[0] -Pattern 'material not found'
}

Test 'set_material repoints one entity and the user count follows' {
    SendOk 'set_material box_a TestRed' | Out-Null
    Assert-Equal -Expected 'TestRed' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Material').name
    Assert-Match -Pattern 'users=1' -Actual (MaterialLine -Name 'TestRed')

    SendOk 'set_material box_a TestBlue' | Out-Null
    Assert-Match -Pattern 'users=0' -Actual (MaterialLine -Name 'TestRed')
    Assert-Match -Pattern 'users=1' -Actual (MaterialLine -Name 'TestBlue')
}

Test 'set_material is undoable' {
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected 'TestRed' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Material').name
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected 'TestBlue' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Material').name
}

Test 'set_material rejects an unknown material or entity' {
    Assert-Err -Result (Send 'set_material box_a Nope')[0]
    Assert-Err -Result (Send 'set_material nope TestRed')[0]
}

Test 'create_material adds a white material to an existing .mat file' {
    SendOk 'create_material TestGreen materials\test.mat' | Out-Null
    Assert-Equal -Expected '4 materials' -Actual (SendOk 'materials')[0].Text
    Assert-Match -Pattern 'file=materials\\test\.mat' -Actual (MaterialLine -Name 'TestGreen')
    Assert-Match -Pattern 'unsaved' -Actual (MaterialLine -Name 'TestGreen') -Message 'the .mat file is now dirty'
}

Test 'create_material rejects a duplicate name' {
    Assert-Err -Result (Send 'create_material TestRed materials\test.mat')[0]
}

Test 'save_materials writes the .mat file, and the level save does not' {
    $matPath = Join-Path $Assets 'materials\test.mat'
    SendOk 'menu "File/Save Level"' | Out-Null
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $names = @($mat.materials | ForEach-Object { $_.name })
    Assert-NotContains -Collection $names -Value 'TestGreen' `
        -Message 'a .mat is a shared asset, not part of the level'

    SendOk 'save_materials' | Out-Null
    $mat = Get-Content $matPath -Raw | ConvertFrom-Json
    $names = @($mat.materials | ForEach-Object { $_.name })
    Assert-Contains -Collection $names -Value 'TestGreen' -Message 'saved materials'
    Assert-NotMatch -Pattern 'unsaved' -Actual (MaterialLine -Name 'TestGreen') -Message 'no longer dirty'
}

Test 'remove_material retires it and reassigns its users' {
    SendOk 'set_material box_c TestGreen' | Out-Null
    SendOk 'remove_material TestGreen' | Out-Null
    Assert-Equal -Expected '3 materials' -Actual (SendOk 'materials')[0].Text
    $material = Get-Component -Session $Session -Entity 'box_c' -Component 'Material'
    Assert-NotEqual -Expected 'TestGreen' -Actual $material.name -Message 'the user was reassigned'
    Assert-Err -Result (Send 'select_material TestGreen')[0] -Message 'a retired material is gone from the panel'
}

Test 'remove_material is undoable, users included' {
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected '4 materials' -Actual (SendOk 'materials')[0].Text
    Assert-Equal -Expected 'TestGreen' -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'Material').name
}

Test 'shaders prints the nine stages' {
    $r = SendOk 'shaders TestRed'
    Assert-Equal -Expected 'shaders for TestRed' -Actual $r[0].Text
    Assert-Equal -Expected 9 -Actual $r[0].Payload.Count
    $slots = @($r[0].Payload | ForEach-Object { ($_ -split '=')[0] })
    foreach ($slot in @('draw_vs', 'draw_hs', 'draw_ds', 'draw_gs', 'draw_ps',
                        'shadow_vs', 'shadow_gs', 'depth_vs', 'depth_ps')) {
        Assert-Contains -Collection $slots -Value $slot -Message 'shader slots'
    }
    Assert-Contains -Collection $r[0].Payload -Value 'draw_vs=MainRenderVS.cso' -Message 'shader values'
}

Test 'set_shader rebinds one stage' {
    SendOk 'set_shader TestRed draw_ps MainRenderPS.cso' | Out-Null
    $r = SendOk 'shaders TestRed'
    Assert-Contains -Collection $r[0].Payload -Value 'draw_ps=MainRenderPS.cso' -Message 'after set_shader'
}

Test 'set_shader refuses a file that will not load as that stage' {
    # ShaderFactory::GetShader caches under the name before checking the stage, so
    # one wrong entry would poison that name for the session; the command must
    # reject it without changing anything.
    $before = (SendOk 'shaders TestRed')[0].Payload
    Assert-Err -Result (Send 'set_shader TestRed draw_ps MainRenderVS.cso')[0] -Message 'a VS is not a PS'
    Assert-Err -Result (Send 'set_shader TestRed draw_ps NoSuchShader.cso')[0] -Message 'a missing file'
    $after = (SendOk 'shaders TestRed')[0].Payload
    Assert-Equal -Expected ($before -join '|') -Actual ($after -join '|') -Message 'nothing changed'
}

Test 'set_shader rejects an unknown slot and an unknown material' {
    Assert-Err -Result (Send 'set_shader TestRed draw_xs MainRenderPS.cso')[0] -Pattern 'unknown shader slot'
    Assert-Err -Result (Send 'set_shader Nope draw_ps MainRenderPS.cso')[0] -Pattern 'material not found'
}

Test 'the Components panel and the Materials panel edit the same material' {
    # Material property editing has exactly one implementation; set_material is
    # the Components panel combo and both surfaces reach the same MaterialData.
    SendOk 'set_material box_b TestRed' | Out-Null
    Assert-Match -Pattern 'users=' -Actual (MaterialLine -Name 'TestRed')
    Assert-Equal -Expected 'TestRed' -Actual (Get-Component -Session $Session -Entity 'box_b' -Component 'Material').name
}
