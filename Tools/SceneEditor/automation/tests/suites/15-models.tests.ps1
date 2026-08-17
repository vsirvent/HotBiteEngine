# fixture: models
# description: The three asset layers - a model is not an object; import, inspect, and turn one into a template.

Test 'list_models reports what each file brought in' {
    $r = SendOk 'list_models'
    Assert-Equal -Expected '3 models' -Actual $r[0].Text
    $tpose = $r[0].Payload | Where-Object { $_ -like 'troll_tpose *' }
    Assert-Match -Pattern 'meshes=1' -Actual $tpose
    Assert-Match -Pattern 'materials=1' -Actual $tpose
    Assert-Match -Pattern 'animations=1' -Actual $tpose
    # An animation-only export carries no mesh - which is the whole reason a model
    # is not an object.
    $walk = $r[0].Payload | Where-Object { $_ -like 'troll_walk *' }
    Assert-Match -Pattern 'meshes=0' -Actual $walk
    Assert-Match -Pattern 'animations=1' -Actual $walk
}

Test 'model_info lists one asset per line' {
    $r = SendOk 'model_info troll_tpose'
    Assert-Match -Pattern 'troll_tpose\.fbx' -Actual $r[0].Text
    Assert-Contains -Collection $r[0].Payload -Value 'mesh troll' -Message 'model assets'
    Assert-True -Condition (@($r[0].Payload | Where-Object { $_ -like 'material *' }).Count -ge 1) -Message 'a material'
    Assert-True -Condition (@($r[0].Payload | Where-Object { $_ -like 'animation *' }).Count -ge 1) -Message 'an animation'
    Assert-Err -Result (Send 'model_info nope')[0] -Pattern 'unknown model'
}

Test 'a model is not placeable' {
    # The registry keeps models and templates apart on purpose: importing an .fbx
    # used to fill the browser with "objects" that spawn nothing.
    Assert-Err -Result (Send 'place troll_tpose')[0]
    Assert-Err -Result (Send 'place troll_walk')[0]
}

Test 'select_model selects, and an unknown one is an error' {
    SendOk 'select_model troll_tpose' | Out-Null
    $line = (SendOk 'list_models')[0].Payload | Where-Object { $_ -like 'troll_tpose *' }
    Assert-Match -Pattern '\[selected\]' -Actual $line
    Assert-Err -Result (Send 'select_model nope')[0] -Pattern 'unknown model'
}

Test 'list_meshes reports the mesh assets a template can point at' {
    $r = SendOk 'list_meshes'
    $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'troll' -Message 'mesh list'
    $troll = $r[0].Payload | Where-Object { $_ -like 'troll *' }
    Assert-Match -Pattern 'animations=' -Actual $troll -Message 'the clips the mesh can play are listed with it'
}

Test 'list_animations reports every clip with the model it came from' {
    $r = SendOk 'list_animations'
    Assert-Equal -Expected '3 animations' -Actual $r[0].Text
    $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    foreach ($clip in @('troll_idle', 'troll_walk', 'troll_tpose')) {
        Assert-Contains -Collection $names -Value $clip -Message 'animation list'
    }
    Assert-Match -Pattern 'model=troll_walk' -Actual ($r[0].Payload | Where-Object { $_ -like 'troll_walk *' })
}

Test 'create_template_from_model builds a template out of the first mesh node' {
    SendOk 'create_template_from_model troll_tpose troll_from_model' | Out-Null
    $blocks = Get-TemplateInfo -Session $Session -Template 'troll_from_model'
    Assert-Equal -Expected 'troll' -Actual $blocks['Mesh'].name -Message 'it took the mesh'
    Assert-True -Condition $blocks.ContainsKey('Material') -Message 'and the material'
    Assert-Ok -Result (Send 'place troll_from_model')[0] -Message 'and it is placeable'
}

Test 'create_template_from_model refuses an animation-only model' {
    Assert-Err -Result (Send 'create_template_from_model troll_walk anim_only')[0] `
        -Message 'no mesh means nothing to make an object out of'
    Assert-Err -Result (Send 'create_template_from_model nope whatever')[0]
}

Test 'import_model copies the file in and registers its assets' {
    $archer = Join-Path $PSScriptRoot '..\..\..\..\..\Tests\DemoGame\assets\Objects\archer.fbx'
    if (-not (Test-Path $archer)) { Skip-Test -Reason 'archer.fbx is not in this checkout' }
    $archer = [IO.Path]::GetFullPath($archer)

    SendOk "import_model ""$archer""" | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Objects\archer.fbx') -Message 'the copy in <assets>/Objects'
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'archer' -Message 'model list'
    # Importing creates no template: that is the separate step.
    $templates = @((SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $templates -Value 'archer' -Message 'template list'
}

Test 'import_model rejects a file that is not there' {
    Assert-Err -Result (Send 'import_model C:\nope\missing.fbx')[0]
    Assert-Err -Result (Send 'import_model')[0] -Pattern 'usage:'
}

Test 'the fixture template spawns a skinned instance with its material' {
    $placed = Get-PlacedInstance -Result (Send 'place tf_troll')[0]
    $mesh = Get-Component -Session $Session -Entity $placed.Name -Component 'Mesh'
    Assert-Equal -Expected 'troll' -Actual $mesh.name
    Assert-Equal -Expected 'TrollMaterial' `
        -Actual (Get-Component -Session $Session -Entity $placed.Name -Component 'Material').name
    $t = Get-Component -Session $Session -Entity $placed.Name -Component 'Transform'
    Assert-Near -Expected 0.025 -Actual $t.scale.x -Tolerance 0.0001 -Message 'the template base scale reached the instance'
}

Test 'a material loaded from a model .mat knows which file it came from' {
    $line = (SendOk 'materials')[0].Payload | Where-Object { $_ -like 'TrollMaterial *' }
    Assert-Match -Pattern 'file=troll\\troll\.mat' -Actual $line
    # A material authored inside the FBX has no file of its own.
    $fbxMaterial = (SendOk 'materials')[0].Payload | Where-Object { $_ -like 'troll *' }
    Assert-Match -Pattern 'file=\(none\)' -Actual $fbxMaterial
}

Test 'a model registered in the level is written back to the models section' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $files = @($level.world.models | ForEach-Object { $_.file })
    Assert-True -Condition ($files.Count -ge 3) -Message 'the level still records its models'
    Assert-True -Condition (@($files | Where-Object { $_ -like '*troll_tpose.fbx' }).Count -eq 1) `
        -Message 'the mesh model is listed once'
}

Test 'import_model can be given a name of its own, and it is the key everything uses' {
    # The name is only a registry key - the meshes, materials and clips inside the
    # file keep their own names - but it is the key the browser, the pickers and the
    # level's "models" array all address the model by. Without this the name was the
    # file stem and nothing else, so two files of the same stem were one model.
    $src = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\Tests\DemoGame\assets\troll\troll_tpose.fbx'))
    if (-not (Test-Path $src)) { Skip-Test -Reason 'the troll assets are not in this checkout' }
    $copy = Join-Path $ShotDir 'renamed_source.fbx'
    Copy-Item $src $copy -Force
    SendOk "import_model ""$copy"" hero_body" | Out-Null
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'hero_body' -Message 'listed under the name it was given'
    Assert-NotContains -Collection $names -Value 'renamed_source' -Message 'and not under the file stem'
    # It really loaded, and the assets are attributed to the new name. Only the clip
    # is new: a model reports what its load *added* to the world's flat collections
    # (World::LoadModel's NewKeys diff), and this is a copy of a file the fixture has
    # already imported, so its mesh and material were there before it.
    Assert-Match -Pattern 'animations=1' -Actual ((SendOk 'list_models')[0].Payload |
        Where-Object { $_ -like 'hero_body *' })
}

Test 'a named model survives the round trip under that name' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $entry = @($level.world.models) | Where-Object { $_.name -eq 'hero_body' }
    Assert-True -Condition ($null -ne $entry) -Message 'the name is written beside the file'
    # A model nothing uses yet still earns its entry once it carries a name: the name
    # exists nowhere else, and the file would only ever say the stem.
    Assert-Match -Pattern 'renamed_source' -Actual $entry.file

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-named-model'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $r = Invoke-EditorCommand -Session $reloaded -Command 'list_models'
        $names = @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $names -Value 'hero_body' -Message 'reloaded under its name'
        # And exactly once: the folder scan must recognize a file it already has by
        # path, or the same .fbx comes back a second time under its stem.
        Assert-NotContains -Collection $names -Value 'renamed_source' -Message 'not adopted twice'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'remove_model takes a model out of the project and out of the level' {
    SendOk 'remove_model hero_body' | Out-Null
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'hero_body' -Message 'gone from the browser'
    Assert-Err -Result (Send 'remove_model hero_body')[0] -Pattern 'unknown model'
    Assert-Err -Result (Send 'remove_model nope')[0] -Pattern 'unknown model'

    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $names = @($level.world.models | ForEach-Object { $_.name })
    Assert-NotContains -Collection $names -Value 'hero_body' -Message 'and out of the saved level'
}