# fixture: empty
# description: The Textures panel's library - load a whole set into Assets/Textures, keep it independent of any material, and remove textures/folders only when nothing uses them.

# A "set" as it sits on the user's disk: two images at the top, two in a nested
# folder, and a readme that is not an image.
$set = Join-Path ([IO.Path]::GetTempPath()) ('hb_texset_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $set 'Rock\Moss') | Out-Null
$png = Join-Path $Assets 'materials\test_red.png'
Copy-Item $png (Join-Path $set 'rock_albedo.png')
Copy-Item $png (Join-Path $set 'rock_normal.png')
Copy-Item $png (Join-Path $set 'Rock\rock_height.png')
Copy-Item $png (Join-Path $set 'Rock\Moss\moss_albedo.png')
Set-Content (Join-Path $set 'readme.txt') 'not a texture'

function Textures { return (SendOk 'list_textures')[0].Payload }

Test 'an empty library lists nothing' {
    Assert-Equal -Expected '0 textures' -Actual (SendOk 'list_textures')[0].Text
}

Test 'create_texture_folder makes a folder that shows even while empty' {
    SendOk 'create_texture_folder Wood\Planks' | Out-Null
    Assert-True -Condition (Test-Path (Join-Path $Assets 'Textures\Wood\Planks')) -Message 'the folder exists on disk'
    Assert-Contains -Collection (SendOk 'texture_folders')[0].Payload -Value 'Wood\Planks'
}

Test 'create_texture_folder refuses a path that escapes Assets/Textures' {
    Assert-Err -Result (Send 'create_texture_folder ..\Elsewhere')[0] -Pattern 'cannot leave'
    Assert-Err -Result (Send 'create_texture_folder C:\Elsewhere')[0] -Pattern 'relative'
}

Test 'import_texture_folder loads a whole set and keeps its structure' {
    $r = SendOk "import_texture_folder $set Sets"
    Assert-Match -Pattern '^4 imported, 1 skipped' -Actual $r[0].Text -Message 'the readme is skipped, not imported'
    $all = Textures
    Assert-Contains -Collection $all -Value 'Sets\rock_albedo.png'
    Assert-Contains -Collection $all -Value 'Sets\Rock\rock_height.png'
    Assert-Contains -Collection $all -Value 'Sets\Rock\Moss\moss_albedo.png'
    Assert-False -Condition (Test-Path (Join-Path $Assets 'Textures\Sets\readme.txt')) -Message 'no non-image was copied'
}

Test 'the library is independent of any material' {
    Assert-Equal -Expected '0 users' -Actual (SendOk 'texture_users Sets\rock_albedo.png')[0].Text
    Assert-Match -Pattern '^diffuse=\(external\)' -Actual (((SendOk 'textures TestRed')[0].Payload) | Where-Object { $_ -like 'diffuse=*' })
}

Test 'import_texture_folder rejects a missing folder and one with no images' {
    Assert-Err -Result (Send 'import_texture_folder C:\definitely\not\here')[0] -Pattern 'folder not found'
    $empty = Join-Path $set 'EmptyDir'
    New-Item -ItemType Directory -Force $empty | Out-Null
    Assert-Err -Result (Send "import_texture_folder $empty")[0] -Pattern 'no images'
}

Test 'texture_users reports every slot that names a texture' {
    SendOk 'set_material_texture TestRed diffuse Sets\rock_albedo.png',
           'set_material_texture TestRed normal Sets\rock_albedo.png',
           'set_material_texture TestBlue diffuse Sets\rock_albedo.png' | Out-Null
    $r = SendOk 'texture_users Sets\rock_albedo.png'
    Assert-Equal -Expected '3 users' -Actual $r[0].Text
    Assert-Contains -Collection $r[0].Payload -Value 'TestRed (diffuse)'
    Assert-Contains -Collection $r[0].Payload -Value 'TestRed (normal)'
    Assert-Contains -Collection $r[0].Payload -Value 'TestBlue (diffuse)'
}

Test 'remove_texture refuses a texture a material still uses, and touches nothing' {
    Assert-Err -Result (Send 'remove_texture Sets\rock_albedo.png')[0] -Pattern 'in use by'
    Assert-FileExists -Path (Join-Path $Assets 'Textures\Sets\rock_albedo.png') -Message 'the file is still there'
    Assert-Contains -Collection (Textures) -Value 'Sets\rock_albedo.png'
}

Test 'remove_texture_folder is all or nothing while anything inside is in use' {
    Assert-Err -Result (Send 'remove_texture_folder Sets')[0] -Pattern 'in use by'
    Assert-FileExists -Path (Join-Path $Assets 'Textures\Sets\Rock\Moss\moss_albedo.png') -Message 'nothing in the folder was deleted'
}

Test 'remove_texture deletes an unused texture from disk and the list' {
    SendOk 'remove_texture Sets\rock_normal.png' | Out-Null
    Assert-False -Condition (Test-Path (Join-Path $Assets 'Textures\Sets\rock_normal.png')) -Message 'the file is gone'
    Assert-NotContains -Collection (Textures) -Value 'Sets\rock_normal.png'
}

Test 'remove_texture rejects a name that is not in the library' {
    Assert-Err -Result (Send 'remove_texture Sets\nope.png')[0] -Pattern 'not an imported texture'
    Assert-Err -Result (Send "remove_texture $png")[0] -Pattern 'not an imported texture'
}

Test 'remove_texture_folder deletes a whole unused folder and prunes what it leaves empty' {
    $r = SendOk 'remove_texture_folder Sets\Rock'
    Assert-Equal -Expected '2 removed' -Actual $r[0].Text
    Assert-False -Condition (Test-Path (Join-Path $Assets 'Textures\Sets\Rock')) -Message 'the folder is gone'
    Assert-NotContains -Collection (Textures) -Value 'Sets\Rock\moss_albedo.png'
}

Test 'remove_texture_folder refuses the library root and an escaping path' {
    Assert-Err -Result (Send 'remove_texture_folder ..')[0] -Pattern 'cannot leave'
    Assert-Err -Result (Send 'remove_texture_folder Nope')[0] -Pattern 'no such folder'
}

Test 'once nothing uses it, the texture can be removed' {
    SendOk 'set_material_texture TestRed diffuse none',
           'set_material_texture TestRed normal none',
           'set_material_texture TestBlue diffuse none' | Out-Null
    SendOk 'remove_texture Sets\rock_albedo.png' | Out-Null
    Assert-False -Condition (Test-Path (Join-Path $Assets 'Textures\Sets')) -Message 'the emptied Sets folder is pruned too'
    Assert-Contains -Collection (SendOk 'texture_folders')[0].Payload -Value 'Wood\Planks' `
        -Message 'a folder made on purpose survives (it was never emptied by a removal)'
}

Test 'select_texture opens the panel, and View/Textures toggles it' {
    SendOk "import_texture_folder $set" | Out-Null
    Assert-Ok -Result (Send 'select_texture rock_albedo.png')
    SendOk 'menu "View/Textures"' | Out-Null
    SendOk 'menu "View/Textures"' | Out-Null
    Assert-Err -Result (Send 'select_texture nope.png')[0] -Pattern 'not an imported texture'
}
