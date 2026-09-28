# fixture: empty
# description: Importing a Meshy AI (or similar) package - an .fbx plus loose diffuse/normal/metallic/roughness maps FBX itself has no slot for - as a model, a material and a placeable, textured, lit, bounded template, all under one Assets subfolder named after it. See MeshyImport.h.

$archer = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\Tests\DemoGame\assets\Objects\archer.fbx'))
if (-not (Test-Path $archer)) { Skip-Test -Reason 'archer.fbx is not in this checkout' }

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('hb_meshyimport_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $scratch | Out-Null

Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
function New-SolidTexture {
    param([string]$Path, [byte]$R, [byte]$G, [byte]$B)
    $bmp = New-Object System.Drawing.Bitmap 8, 8
    $color = [System.Drawing.Color]::FromArgb(255, $R, $G, $B)
    for ($y = 0; $y -lt 8; $y++) {
        for ($x = 0; $x -lt 8; $x++) { $bmp.SetPixel($x, $y, $color) }
    }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# Builds a folder shaped like a Meshy export: <stem>.fbx (a renamed copy of archer.fbx)
# plus whichever loose maps $Suffixes names, each a tiny solid-color PNG -
# "<stem><suffix>.png". "" is the diffuse map (no suffix at all), matching Meshy's own
# naming.
function New-MeshyPackage {
    param([string]$Dir, [string]$Stem, [string[]]$Suffixes = @('', '_normal', '_metallic', '_roughness'))
    New-Item -ItemType Directory -Force $Dir | Out-Null
    Copy-Item $archer (Join-Path $Dir "$Stem.fbx") -Force
    foreach ($suffix in $Suffixes) {
        New-SolidTexture -Path (Join-Path $Dir "$Stem$suffix.png") -R 180 -G 140 -B 100
    }
}

function TextureLine {
    param($Session2, [string]$Material, [string]$Slot)
    $r = Invoke-EditorCommand -Session $Session2 -Command "textures $Material"
    Assert-Ok -Result $r[0]
    return ($r[0].Payload | Where-Object { $_ -like "$Slot=*" })
}

# Every scenario that ends in a *successful* import needs archer.fbx's mesh node to
# still be unclaimed - FBXLoader::ProcessEntity dedupes by node name across every
# model this World has ever loaded (see World.h's templates-coordinator note), so a
# second import of the same content under a second name in the *same* session finds
# its node already spoken for and CreateFromModel reports "no mesh" - not a bug in
# this import, but the same dedup an ordinary File/Import Model hits (15-models.tests.ps1
# exercises it as a feature: re-importing a copy of an already-loaded file adds no new
# mesh). So each such scenario gets its own fresh session; only the checks that fail
# before AssetBrowser::ImportModel ever runs (name collisions, a missing source, a bad
# lod argument) share $Session with the first successful import.
function New-MeshySession {
    param([string]$Suffix)
    $dir = Join-Path (Split-Path -Parent $ShotDir) "meshy-$Suffix"
    return New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $dir
}

$pkg1 = Join-Path $scratch 'pkg1'
New-MeshyPackage -Dir $pkg1 -Stem 'stonebridge'

Test 'import_meshy_model builds a model, a material and a placeable, bounded template' {
    $r = SendOk "import_meshy_model ""$pkg1"" meshy_bridge"
    Assert-Match -Pattern '4 texture map\(s\)' -Actual $r[0].Text
    Assert-Match -Pattern '0 LOD level\(s\)' -Actual $r[0].Text -Message 'no lod_ratios given'

    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-Contains -Collection $names -Value 'meshy_bridge' -Message 'model list'

    $blocks = Get-TemplateInfo -Session $Session -Template 'meshy_bridge'
    Assert-True -Condition $blocks.ContainsKey('Mesh') -Message 'template has a Mesh'
    Assert-Equal -Expected 'meshy_bridge' -Actual $blocks['Material'].name -Message 'wearing the material this import built, not the FBX''s own'
    Assert-True -Condition $blocks.ContainsKey('Bounds') -Message 'template has Bounds - without it RenderSystem never draws it (drawable_signature)'
    Assert-True -Condition ($blocks['Bounds'].extents.x -gt 0 -or $blocks['Bounds'].extents.y -gt 0 -or $blocks['Bounds'].extents.z -gt 0) `
        -Message 'a real box, not a zeroed placeholder'

    Assert-Ok -Result (Send 'place meshy_bridge')[0] -Message 'placeable'
}

Test 'diffuse and normal are wired on, and metallic (present alongside roughness) wins the plain spec slot' {
    Assert-Match -Pattern '^diffuse=meshy_bridge\\stonebridge\.png$' -Actual (TextureLine $Session 'meshy_bridge' 'diffuse')
    Assert-Match -Pattern '^normal=meshy_bridge\\stonebridge_normal\.png$' -Actual (TextureLine $Session 'meshy_bridge' 'normal')
    Assert-Match -Pattern '^specular=meshy_bridge\\stonebridge_metallic\.png$' -Actual (TextureLine $Session 'meshy_bridge' 'specular') `
        -Message 'the engine has no metallic workflow, so metallic stands in for spec - and beats roughness when both are present'
    Assert-Equal -Expected 'ao=' -Actual (TextureLine $Session 'meshy_bridge' 'ao') -Message 'this package has no AO map'
}

Test 'the model and its textures land under one Assets subfolder named after the template' {
    Assert-FileExists -Path (Join-Path $Assets 'Objects\meshy_bridge\stonebridge.fbx') -Message 'the model, under its own subfolder'
    Assert-FileExists -Path (Join-Path $Assets 'Textures\meshy_bridge\stonebridge.png') -Message 'the diffuse map'
    Assert-FileExists -Path (Join-Path $Assets 'Textures\meshy_bridge\stonebridge_normal.png') -Message 'the normal map'
    Assert-FileExists -Path (Join-Path $Assets 'Textures\meshy_bridge\stonebridge_metallic.png') -Message 'the metallic map, imported as-is (no synthesized texture to write)'
}

Test 'a name collision on the material or the template is refused before anything is imported' {
    # tf_box is one of the fixture's own templates (New-TestProject.ps1's "empty" kind).
    Assert-Err -Result (Send "import_meshy_model ""$pkg1"" tf_box")[0] -Pattern 'template.*already exists'
    $names = @((SendOk 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
    Assert-NotContains -Collection $names -Value 'tf_box' -Message 'refused before AssetBrowser::ImportModel ever ran'

    SendOk 'create_material meshy_taken materials\test.mat' | Out-Null
    Assert-Err -Result (Send "import_meshy_model ""$pkg1"" meshy_taken")[0] -Pattern 'material.*already exists'
}

Test 'import_meshy_model rejects a missing source and reports its own usage' {
    Assert-Err -Result (Send 'import_meshy_model C:\nope\missing')[0]
    Assert-Err -Result (Send 'import_meshy_model')[0] -Pattern 'usage:'
}

Test 'a malformed lod argument is refused rather than silently building no chain' {
    # Never reaches the filesystem - the lod argument is parsed before any import
    # work starts - so the path itself does not have to be a real package.
    Assert-Err -Result (Send "import_meshy_model ""$pkg1"" meshy_bad_lod notanumber")[0] -Pattern 'lod argument'
}

$pkg2 = Join-Path $scratch 'pkg2'
New-MeshyPackage -Dir $pkg2 -Stem 'lonestone' -Suffixes @('', '_ao', '_roughness')

Test 'AO goes to its own slot, and roughness alone (no metallic) falls back to spec' {
    $s2 = New-MeshySession -Suffix 'ao'
    try {
        Invoke-EditorCommand -Session $s2 -Command "import_meshy_model ""$pkg2"" meshy_lone_ao" | Out-Null
        Assert-Match -Pattern '^ao=meshy_lone_ao\\lonestone_ao\.png$' -Actual (TextureLine $s2 'meshy_lone_ao' 'ao')
        Assert-Match -Pattern '^specular=meshy_lone_ao\\lonestone_roughness\.png$' -Actual (TextureLine $s2 'meshy_lone_ao' 'specular') `
            -Message 'roughness is the last resort for spec, used only when no metallic (or explicit specular) map is present'
    }
    finally {
        Close-EditorSession -Session $s2
    }
}

$pkg3 = Join-Path $scratch 'pkg3'
New-MeshyPackage -Dir $pkg3 -Stem 'synonympiece' -Suffixes @('', '_metalness', '_occlusion')

Test 'metallic and AO synonyms are recognized alongside Meshy''s own vocabulary' {
    $s3 = New-MeshySession -Suffix 'synonyms'
    try {
        Invoke-EditorCommand -Session $s3 -Command "import_meshy_model ""$pkg3"" meshy_synonyms" | Out-Null
        Assert-Match -Pattern '^specular=meshy_synonyms\\synonympiece_metalness\.png$' -Actual (TextureLine $s3 'meshy_synonyms' 'specular') `
            -Message '_metalness classifies the same as _metallic'
        Assert-Match -Pattern '^ao=meshy_synonyms\\synonympiece_occlusion\.png$' -Actual (TextureLine $s3 'meshy_synonyms' 'ao') `
            -Message '_occlusion classifies the same as _ao'
    }
    finally {
        Close-EditorSession -Session $s3
    }
}

$pkg4 = Join-Path $scratch 'pkg4'
New-MeshyPackage -Dir $pkg4 -Stem 'twofbx' -Suffixes @('')
Copy-Item $archer (Join-Path $pkg4 'twofbx_extra.fbx') -Force

Test 'a folder holding more than one .fbx is refused, and picking the file directly disambiguates it' {
    $s4 = New-MeshySession -Suffix 'ambiguous'
    try {
        Assert-Err -Result (Invoke-EditorCommand -Session $s4 -Command "import_meshy_model ""$pkg4"" meshy_ambiguous")[0] `
            -Pattern 'more than one \.fbx'
        $names = @((Invoke-EditorCommand -Session $s4 -Command 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
        Assert-NotContains -Collection $names -Value 'meshy_ambiguous' -Message 'nothing left behind by the refused attempt'

        Invoke-EditorCommand -Session $s4 -Command "import_meshy_model ""$(Join-Path $pkg4 'twofbx.fbx')"" meshy_disambiguated" | Out-Null
        $names = @((Invoke-EditorCommand -Session $s4 -Command 'list_models')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $names -Value 'meshy_disambiguated' -Message 'picking the .fbx file itself skips the folder search'
    }
    finally {
        Close-EditorSession -Session $s4
    }
}

$pkg5 = Join-Path $scratch 'pkg5'
New-MeshyPackage -Dir $pkg5 -Stem 'zippiece'

Test 'import_meshy_model extracts a .zip package' {
    $s5 = New-MeshySession -Suffix 'zip'
    try {
        $zipPath = Join-Path $scratch 'pkg5.zip'
        Compress-Archive -Path (Join-Path $pkg5 '*') -DestinationPath $zipPath -Force
        $r = Invoke-EditorCommand -Session $s5 -Command "import_meshy_model ""$zipPath"" meshy_from_zip"
        Assert-Ok -Result $r[0]
        Assert-Match -Pattern '4 texture map\(s\)' -Actual $r[0].Text
        Assert-Match -Pattern '^diffuse=meshy_from_zip\\zippiece\.png$' -Actual (TextureLine $s5 'meshy_from_zip' 'diffuse')
    }
    finally {
        Close-EditorSession -Session $s5
    }
}

$pkg6 = Join-Path $scratch 'pkg6'
New-MeshyPackage -Dir $pkg6 -Stem 'lodpiece'

Test 'a level count builds the usual halving sequence and installs it on the template' {
    $s6 = New-MeshySession -Suffix 'lods'
    try {
        Invoke-EditorCommand -Session $s6 -Command "import_meshy_model ""$pkg6"" meshy_lods 3" | Out-Null
        $blocks = Get-TemplateInfo -Session $s6 -Template 'meshy_lods'
        $lods = @($blocks['Mesh'].lods)
        Assert-Equal -Expected 3 -Actual $lods.Count
        #Named after the import, not after the .fbx node its mesh came from: unrelated
        #exports share node names, and their levels must not.
        for ($i = 1; $i -le 3; $i++) {
            Assert-Equal -Expected "meshy_lods_lod$i" -Actual $lods[$i - 1].name
        }
        $meshNames = @((Invoke-EditorCommand -Session $s6 -Command 'list_meshes')[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
        Assert-Contains -Collection $meshNames -Value 'meshy_lods_lod1' -Message 'registered as a mesh asset like any other'
        Assert-Contains -Collection $meshNames -Value 'meshy_lods_lod3'
        Assert-FileExists -Path (Join-Path $Assets 'Objects\meshy_lods\meshy_lods_lod1.hbmesh') -Message 'the cached geometry, beside the model'
        Assert-FileExists -Path (Join-Path $Assets 'Objects\meshy_lods\meshy_lods_lod3.hbmesh')
        Assert-True -Condition (-not (Test-Path (Join-Path $Assets 'GeneratedMeshes'))) -Message 'nothing goes to the shared GeneratedMeshes folder'
    }
    finally {
        Close-EditorSession -Session $s6
    }
}
