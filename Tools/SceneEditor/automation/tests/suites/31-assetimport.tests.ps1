# fixture: empty
# description: Textures and shaders are imported into the project (Assets/Textures with subfolders, Assets/Shaders) and picked from there - a material slot never points at an arbitrary file on disk.

# A source image that lives OUTSIDE Assets/Textures, the way a file from the user's
# own disk does. The test project ships test_red.png beside its .mat file.
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('hb_assetimport_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $scratch | Out-Null
$source = Join-Path $scratch 'wood_planks.png'
Copy-Item (Join-Path $Assets 'materials\test_red.png') $source

function TextureLine {
    param([string]$Material, [string]$Slot)
    $r = SendOk "textures $Material"
    return ($r[0].Payload | Where-Object { $_ -like "$Slot=*" })
}

Test 'import_texture copies the file into Assets/Textures' {
    $r = SendOk "import_texture $($source)"
    $dest = Join-Path $Assets 'Textures\wood_planks.png'
    Assert-FileExists -Path $dest -Message 'the imported copy'
    Assert-True -Condition (Test-Path $source) -Message 'the original is left where it was'
    Assert-Match -Pattern 'Assets\\Textures\\wood_planks\.png$' -Actual $r[0].Text
}

Test 'import_texture honours a subfolder and list_textures reports it relative' {
    SendOk "import_texture $($source) Wood\Planks" | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Textures\Wood\Planks\wood_planks.png') -Message 'the subfolder copy'
    $r = SendOk 'list_textures'
    Assert-Contains -Collection $r[0].Payload -Value 'wood_planks.png'
    Assert-Contains -Collection $r[0].Payload -Value 'Wood\Planks\wood_planks.png'
}

Test 'import_texture rejects a missing file, a non-image, and a subfolder that escapes' {
    Assert-Err -Result (Send 'import_texture C:\definitely\not\here.png')[0] -Pattern 'file not found'
    $txt = Join-Path $scratch 'notes.txt'
    Set-Content $txt 'hello'
    Assert-Err -Result (Send "import_texture $txt")[0] -Pattern 'not an image'
    Assert-Err -Result (Send "import_texture $($source) ..\Outside")[0] -Pattern 'cannot leave'
    Assert-Err -Result (Send "import_texture $($source) C:\Elsewhere")[0] -Pattern 'relative'
}

Test 'a material created before this rule reports its texture as external' {
    Assert-Match -Pattern '^diffuse=\(external\)' -Actual (TextureLine -Material 'TestRed' -Slot 'diffuse')
}

Test 'set_material_texture picks an imported texture by name' {
    SendOk 'set_material_texture TestRed diffuse Wood\Planks\wood_planks.png' | Out-Null
    Assert-Equal -Expected 'diffuse=Wood\Planks\wood_planks.png' -Actual (TextureLine -Material 'TestRed' -Slot 'diffuse')
}

Test 'set_material_texture refuses anything that is not imported' {
    Assert-Err -Result (Send "set_material_texture TestRed diffuse $($source)")[0] -Pattern 'not an imported texture'
    Assert-Err -Result (Send 'set_material_texture TestRed nope wood_planks.png')[0] -Pattern 'unknown texture slot'
    Assert-Err -Result (Send 'set_material_texture Nope diffuse wood_planks.png')[0] -Pattern 'material not found'
}

Test 'set_material_texture is undoable' {
    SendOk 'undo' | Out-Null
    Assert-Match -Pattern '^diffuse=\(external\)' -Actual (TextureLine -Material 'TestRed' -Slot 'diffuse') `
        -Message 'undo put the previous texture back'
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected 'diffuse=Wood\Planks\wood_planks.png' -Actual (TextureLine -Material 'TestRed' -Slot 'diffuse')
}

Test 'an imported texture is saved as a relative path, so the level is portable' {
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
    $rec = $mat.materials | Where-Object { $_.name -eq 'TestRed' }
    # The .mat's own root is Assets\materials\, so Assets\Textures is reached with "..".
    Assert-Equal -Expected '..\Textures\Wood\Planks\wood_planks.png' -Actual $rec.diffuse_textname
    Assert-NotMatch -Pattern '^[A-Za-z]:' -Actual $rec.diffuse_textname -Message 'no drive letter in a saved path'
}

Test 'the saved relative path resolves to the imported file from the .mat root' {
    # Load builds root + "\" + file, so this is exactly what the next load opens.
    $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
    $rec = $mat.materials | Where-Object { $_.name -eq 'TestRed' }
    $resolved = [IO.Path]::GetFullPath((Join-Path (Join-Path $Assets 'materials') $rec.diffuse_textname))
    Assert-FileExists -Path $resolved -Message 'the file the saved path points at'
    Assert-Match -Pattern 'Textures\\Wood\\Planks\\wood_planks\.png$' -Actual $resolved
}

Test 'set_material_texture none clears the slot' {
    SendOk 'set_material_texture TestRed diffuse none' | Out-Null
    Assert-Equal -Expected 'diffuse=' -Actual (TextureLine -Material 'TestRed' -Slot 'diffuse')
}

$shaderSource = Join-Path $scratch 'TestImportedPS.hlsl'
[IO.File]::WriteAllText($shaderSource,
    "float4 main(float4 position : SV_POSITION) : SV_TARGET`r`n{`r`n`treturn float4(1.0f, 0.0f, 1.0f, 1.0f);`r`n}`r`n",
    (New-Object Text.UTF8Encoding($false)))

Test 'import_shader brings a .hlsl into Assets/Shaders, compiled beside it' {
    $r = SendOk "import_shader $($shaderSource)"
    Assert-Equal -Expected 'TestImportedPS.cso' -Actual ($r[0].Text -replace '^OK ', '')
    Assert-FileExists -Path (Join-Path $Assets 'Shaders\TestImportedPS.hlsl') -Message 'the source copy'
    Assert-FileExists -Path (Join-Path $Assets 'Shaders\TestImportedPS.cso') -Message 'the compiled binary'
}

Test 'an imported shader can be assigned to a material by name' {
    SendOk 'set_shader TestRed draw_ps TestImportedPS.cso' | Out-Null
    Assert-Contains -Collection (SendOk 'shaders TestRed')[0].Payload -Value 'draw_ps=TestImportedPS.cso'
    SendOk 'set_shader TestRed draw_ps MainRenderPS.cso' | Out-Null
}

Test 'import_shader rejects a name with no stage suffix and a wrong file type' {
    $bad = Join-Path $scratch 'Plain.hlsl'
    Set-Content $bad 'float4 main() : SV_TARGET { return 0; }'
    Assert-Err -Result (Send "import_shader $bad")[0] -Pattern 'must end with'
    Assert-Err -Result (Send "import_shader $($source)")[0] -Pattern '\.hlsl'
    Assert-Err -Result (Send 'import_shader C:\definitely\not\here.hlsl')[0] -Pattern 'file not found'
}

Test 'import_shader reports a compile error instead of half-importing' {
    $broken = Join-Path $scratch 'BrokenPS.hlsl'
    Set-Content $broken 'this is not hlsl'
    Assert-Err -Result (Send "import_shader $broken")[0] -Pattern 'compile failed'
    Assert-False -Condition (Test-Path (Join-Path $Assets 'Shaders\BrokenPS.cso')) -Message 'no .cso from a failed compile'
}
