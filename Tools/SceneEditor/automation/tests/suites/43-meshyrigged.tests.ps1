# fixture: empty
# description: Importing a rigged Meshy package - "<base>_Character_output.fbx" (skinned model), one "<base>_Animation_<Clip>_without_skin.fbx" per clip, and "<base>_texture_0*.png" maps - as a model, one animation model per clip, a material and a template carrying a clip library. See MeshyImport.h.

$troll = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\Tests\DemoGame\assets\troll'))
if (-not (Test-Path (Join-Path $troll 'troll_tpose.fbx'))) { Skip-Test -Reason 'the troll assets are not in this checkout' }

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('hb_meshyrigged_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $scratch | Out-Null

Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
function New-SolidTexture {
    param([string]$Path)
    $bmp = New-Object System.Drawing.Bitmap 8, 8
    for ($y = 0; $y -lt 8; $y++) { for ($x = 0; $x -lt 8; $x++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 180, 140, 100)) } }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# A folder shaped like a rigged Meshy export. The troll stands in for the character
# (a skinned mesh) and for the clip files (skeleton and motion, no mesh): the importer
# only cares about the naming and about what the FBXs contain, not whose rig it is.
function New-RiggedPackage {
    param([string]$Dir, [string]$Base, [string[]]$Clips = @('Idle', 'Walking'))
    New-Item -ItemType Directory -Force $Dir | Out-Null
    Copy-Item (Join-Path $troll 'troll_tpose.fbx') (Join-Path $Dir "${Base}_Character_output.fbx") -Force
    $sources = @{ Idle = 'troll_idle.fbx'; Walking = 'troll_walk.fbx'; Attack = 'troll_attack.fbx' }
    foreach ($clip in $Clips) {
        Copy-Item (Join-Path $troll $sources[$clip]) (Join-Path $Dir "${Base}_Animation_${clip}_without_skin.fbx") -Force
    }
    New-SolidTexture -Path (Join-Path $Dir "${Base}_texture_0.png")
    New-SolidTexture -Path (Join-Path $Dir "${Base}_texture_0_normal.png")
}

function New-RiggedSession {
    param([string]$Suffix)
    $dir = Join-Path (Split-Path -Parent $ShotDir) "meshyrigged-$Suffix"
    return New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $dir
}

$pkg = Join-Path $scratch 'pkg'
New-RiggedPackage -Dir $pkg -Base 'Meshy_AI_Hero'

Test 'a folder with a character file and clip files is not "more than one .fbx"' {
    $r = SendOk "import_meshy_model ""$pkg"" hero"
    Assert-Match -Pattern 'rigged, 2 animation\(s\)' -Actual $r[0].Text
    Assert-Match -Pattern '2 texture map\(s\)' -Actual $r[0].Text -Message 'matched against <base>_texture_0, not the character file name'
}

Test 'the character and every clip file become models, named <name>_<clip>' {
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    foreach ($m in 'hero', 'hero_idle', 'hero_walk') {
        Assert-Contains -Collection $names -Value $m -Message 'model list'
    }
}

Test 'the template wears the new material and carries a clip library with an idle default' {
    $blocks = Get-TemplateInfo -Session $Session -Template 'hero'
    Assert-Equal -Expected 'hero' -Actual $blocks['Material'].name
    Assert-True -Condition $blocks.ContainsKey('Bounds') -Message 'bounded, or it is never drawn'
    $clips = $blocks['Mesh'].clips
    Assert-True -Condition ($null -ne $clips.idle) -Message 'idle clip'
    Assert-True -Condition ($null -ne $clips.walk) -Message 'Walking is offered as the usual role name, walk'
    Assert-Equal -Expected 'idle' -Actual $blocks['Mesh'].animation -Message 'plays idle by default'
    Assert-Ok -Result (Send 'place hero')[0] -Message 'placeable'
}

Test 'the template stands upright: its long axis ends up on Y, not Z' {
    # A Z-up export either carries the correction in its node rotation or gets one
    # from the importer; either way the rotated bounds must be taller than deep.
    $blocks = Get-TemplateInfo -Session $Session -Template 'hero'
    $q = $blocks['Transform'].rotation
    $e = $blocks['Bounds'].extents
    $up = [math]::Abs(2 * ($q.x * $q.y + $q.z * $q.w)) * $e.x + [math]::Abs(1 - 2 * ($q.x * $q.x + $q.z * $q.z)) * $e.y + [math]::Abs(2 * ($q.y * $q.z - $q.x * $q.w)) * $e.z
    $depth = [math]::Abs(2 * ($q.x * $q.z - $q.y * $q.w)) * $e.x + [math]::Abs(2 * ($q.y * $q.z + $q.x * $q.w)) * $e.y + [math]::Abs(1 - 2 * ($q.x * $q.x + $q.y * $q.y)) * $e.z
    Assert-True -Condition ($up -ge $depth * 0.8) -Message "height $up vs depth $depth"
}

Test 'everything lands under one subfolder per asset kind' {
    Assert-FileExists -Path (Join-Path $Assets 'Objects\hero\Meshy_AI_Hero_Character_output.fbx')
    Assert-FileExists -Path (Join-Path $Assets 'Objects\hero\Meshy_AI_Hero_Animation_Idle_without_skin.fbx')
    Assert-FileExists -Path (Join-Path $Assets 'Textures\hero\Meshy_AI_Hero_texture_0.png')
    Assert-FileExists -Path (Join-Path $Assets 'Textures\hero\Meshy_AI_Hero_texture_0_normal.png')
}

Test 'a folder of clip files alone is refused: there is no character to build a template from' {
    $only = Join-Path $scratch 'only_clips'
    New-Item -ItemType Directory -Force $only | Out-Null
    Copy-Item (Join-Path $troll 'troll_idle.fbx') (Join-Path $only 'X_Animation_Idle_without_skin.fbx')
    Assert-Err -Result (Send "import_meshy_model ""$only"" nochar")[0] -Pattern 'no character'
}

Test 'a clip model name already taken is refused before anything is imported' {
    # A model already called clash_walk, so a package named "clash" with a Walking clip
    # would need the same name for its clip model.
    $walk = Join-Path $scratch 'clash_walk_source.fbx'
    Copy-Item (Join-Path $troll 'troll_walk.fbx') $walk -Force
    SendOk "import_model ""$walk"" clash_walk" | Out-Null
    $dup = Join-Path $scratch 'dup'
    New-RiggedPackage -Dir $dup -Base 'Meshy_AI_Dup' -Clips @('Walking')
    Assert-Err -Result (Send "import_meshy_model ""$dup"" clash")[0] -Pattern "model named 'clash_walk' already exists"
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'clash' -Message 'nothing imported by the refused attempt'
}

Test 'a rigged package survives save and reopen: the clips still resolve on the template' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $reloaded = New-RiggedSession -Suffix 'reload'
    try {
        $r = Invoke-EditorCommand -Session $reloaded -Command 'template_animations hero'
        Assert-Ok -Result $r[0]
        $lines = @($r[0].Payload)
        Assert-True -Condition (@($lines | Where-Object { $_ -like 'idle *' -and $_ -match 'model=hero_idle' }).Count -eq 1) -Message 'idle resolves to its clip model'
        Assert-True -Condition (@($lines | Where-Object { $_ -like 'walk *' -and $_ -match 'model=hero_walk' }).Count -eq 1) -Message 'walk resolves to its clip model'
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}
