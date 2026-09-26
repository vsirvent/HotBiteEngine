# fixture: empty
# description: Poly Haven as a material source - the catalog by category, previews, and importing an asset's maps into Assets/Textures/PolyHaven/<id> as an undoable material. Runs offline against a local mirror of the API; one test touches the real site and skips when offline.

# A local folder laid out like https://api.polyhaven.com: categories/textures,
# assets (the ?t=textures query is dropped for a folder), files/<id>. Map URLs are
# relative to the folder, thumbnails absolute - both forms have to resolve.
$src = Join-Path ([IO.Path]::GetTempPath()) ('hb_polyhaven_' + [Guid]::NewGuid().ToString('N'))
$png = Join-Path $Assets 'materials\test_red.png'
New-Item -ItemType Directory -Force (Join-Path $src 'categories'), (Join-Path $src 'files'), (Join-Path $src 'maps') | Out-Null
foreach ($f in 'red_brick_diff_1k', 'red_brick_diff_2k', 'red_brick_nor_dx_2k', 'red_brick_arm_2k',
               'red_brick_disp_2k', 'plank_wood_diff_1k', 'plank_wood_ao_1k', 'thumb') {
    Copy-Item $png (Join-Path $src "maps\$f.png")
}
function Write-Fixture([string]$Rel, $Value) {
    [IO.File]::WriteAllText((Join-Path $src $Rel), ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}
function Map([hashtable]$ByRes) {
    $m = @{}
    foreach ($res in $ByRes.Keys) { $m[$res] = @{ png = @{ url = $ByRes[$res]; size = 0 } } }
    return $m
}
$thumb = Join-Path $src 'maps\thumb.png'
Write-Fixture 'categories\textures' ([ordered]@{ brick = 2; all = 3; wood = 1 })
Write-Fixture 'assets' @{
    red_brick  = @{ name = 'Red Brick';  categories = @('brick'); tags = @('wall', 'clay'); download_count = 10; thumbnail_url = $thumb }
    old_brick  = @{ name = 'Old Brick';  categories = @('brick'); tags = @('ruin');         download_count = 5;  thumbnail_url = $thumb }
    plank_wood = @{ name = 'Plank Wood'; categories = @('wood');  tags = @('floor');        download_count = 20; thumbnail_url = '' }
}
# Full set at 1k/2k. Only png is offered, so the jpg preference falls through.
Write-Fixture 'files\red_brick' @{
    Diffuse      = Map @{ '1k' = 'maps/red_brick_diff_1k.png'; '2k' = 'maps/red_brick_diff_2k.png' }
    nor_dx       = Map @{ '2k' = 'maps/red_brick_nor_dx_2k.png' }
    arm          = Map @{ '2k' = 'maps/red_brick_arm_2k.png' }
    Displacement = Map @{ '2k' = 'maps/red_brick_disp_2k.png' }
}
# No arm map (AO instead), and only 1k.
Write-Fixture 'files\plank_wood' @{
    Diffuse = Map @{ '1k' = 'maps/plank_wood_diff_1k.png' }
    AO      = Map @{ '1k' = 'maps/plank_wood_ao_1k.png' }
}
# Not a texture at all.
Write-Fixture 'files\old_brick' @{ blend = Map @{ '1k' = 'maps/nothing.blend' } }

function Status { return (SendOk 'polyhaven_status')[0] }

function Wait-Catalog {
    param([int]$TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $s = Status
        if ($s.Text -notmatch 'catalog=(loading|idle)') { return $s }
        Start-Sleep -Milliseconds 100
    }
    throw "the catalog did not load within ${TimeoutSec}s"
}

# An import answers when it is queued; the material appears once Tick has run.
function Wait-Import {
    param([int]$TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $s = Status
        if ($s.Text -match 'import=idle') { return $s }
        Start-Sleep -Milliseconds 100
    }
    throw "the import did not finish within ${TimeoutSec}s"
}

function Last([object]$S) { return ($S.Payload | Where-Object { $_ -like 'last=*' }) }
function Slot([string]$Material, [string]$Name) {
    return (((SendOk "textures $Material")[0].Payload) | Where-Object { $_ -like "$Name=*" }) -replace "^$Name=", ''
}

Test 'polyhaven_source points the catalog at a local mirror and starts it empty' {
    Assert-Equal -Expected $src -Actual (SendOk "polyhaven_source `"$src`"")[0].Text
    Assert-Match -Pattern 'catalog=idle assets=0 import=idle' -Actual (Status).Text
}

Test 'polyhaven_list refuses until the catalog has loaded' {
    Assert-Err -Result (Send 'polyhaven_list')[0] -Pattern 'not loaded'
}

Test 'polyhaven_refresh loads the catalog in the background' {
    Assert-Equal -Expected 'loading' -Actual (SendOk 'polyhaven_refresh')[0].Text
    Assert-Match -Pattern 'catalog=ready assets=3' -Actual (Wait-Catalog).Text
}

Test 'categories come back largest first, whatever order the JSON had them in' {
    $r = SendOk 'polyhaven_categories'
    Assert-Equal -Expected '3 categories' -Actual $r[0].Text
    Assert-Equal -Expected 'all=3' -Actual $r[0].Payload[0]
    Assert-Equal -Expected 'brick=2' -Actual $r[0].Payload[1]
}

Test 'the catalog is most-downloaded first and filters by category and by tag' {
    Assert-Equal -Expected 'plank_wood,red_brick,old_brick' -Actual ((SendOk 'polyhaven_list')[0].Payload -join ',')
    Assert-Equal -Expected 'red_brick,old_brick' -Actual ((SendOk 'polyhaven_list brick')[0].Payload -join ',')
    Assert-Equal -Expected 'red_brick' -Actual ((SendOk 'polyhaven_list all clay')[0].Payload -join ',') `
        -Message 'a tag matches, not only the name'
    Assert-Equal -Expected '0 assets' -Actual (SendOk 'polyhaven_list wood brick')[0].Text
}

Test 'a preview is fetched in the background and loads as a texture' {
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $t = (SendOk 'polyhaven_thumb red_brick')[0].Text
        if ($t -ne 'loading') { break }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    Assert-Equal -Expected 'ready' -Actual $t
    Assert-Equal -Expected 'failed' -Actual (SendOk 'polyhaven_thumb plank_wood')[0].Text `
        -Message 'an asset with no preview fails at once instead of loading forever'
}

Test 'polyhaven_import rejects bad input before starting anything' {
    Assert-Err -Result (Send 'polyhaven_import ..\evil')[0] -Pattern 'not a Poly Haven asset id'
    Assert-Err -Result (Send 'polyhaven_import red_brick 3k')[0] -Pattern 'resolution must be'
    Assert-Err -Result (Send 'polyhaven_import red_brick nope.mat')[0] -Pattern 'not one of this level'
    Assert-Err -Result (Send 'polyhaven_import red_brick sideways')[0] -Pattern 'unknown option'
    Assert-Match -Pattern 'import=idle' -Actual (Status).Text
}

Test 'importing downloads the maps into their own folder and creates the material' {
    Assert-Equal -Expected 'started red_brick' -Actual (SendOk 'polyhaven_import red_brick 2k')[0].Text
    $s = Wait-Import
    Assert-Match -Pattern '^last=ok red_brick \(2k, 4 maps\) added to materials\\test\.mat' -Actual (Last $s)
    $dir = Join-Path $Assets 'Textures\PolyHaven\red_brick'
    foreach ($f in 'red_brick_diff_2k.png', 'red_brick_nor_dx_2k.png', 'red_brick_arm_2k.png', 'red_brick_disp_2k.png') {
        Assert-FileExists -Path (Join-Path $dir $f)
    }
    Assert-False -Condition (Test-Path (Join-Path $dir 'red_brick_diff_1k.png')) -Message 'only the resolution asked for'
    Assert-Equal -Expected 0 -Actual @(Get-ChildItem $dir -Filter '*.part').Count -Message 'no partial files left behind'
    Assert-Contains -Collection (SendOk 'list_textures')[0].Payload -Value 'PolyHaven\red_brick\red_brick_diff_2k.png' `
        -Message 'the Textures panel sees them like any imported texture'
}

Test 'the maps land in the slots the engine reads them from' {
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_diff_2k.png' -Actual (Slot red_brick diffuse)
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_nor_dx_2k.png' -Actual (Slot red_brick normal)
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_arm_2k.png' -Actual (Slot red_brick arm)
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_disp_2k.png' -Actual (Slot red_brick height)
    Assert-Equal -Expected '' -Actual (Slot red_brick ao) -Message 'ARM carries the AO, so the ao slot stays empty'
    Assert-Match -Pattern 'unsaved' -Actual ((SendOk 'materials')[0].Payload | Where-Object { $_ -like 'red_brick *' })
}

Test 'an import is two undo steps: the maps, then the material' {
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected '' -Actual (Slot red_brick diffuse)
    SendOk 'undo' | Out-Null
    Assert-Err -Result (Send 'textures red_brick')[0] -Pattern 'material not found'
    SendOk 'redo', 'redo' | Out-Null
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_diff_2k.png' -Actual (Slot red_brick diffuse)
}

Test 'an imported material applies like any other and saves with relative paths' {
    SendOk 'set_material box_a red_brick' | Out-Null
    Assert-Equal -Expected 'red_brick' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Material').name
    SendOk 'save_materials' | Out-Null
    $mat = Get-Content (Join-Path $Assets 'materials\test.mat') -Raw | ConvertFrom-Json
    $saved = $mat.materials | Where-Object { $_.name -eq 'red_brick' }
    Assert-True -Condition ($null -ne $saved) -Message 'red_brick was written to test.mat'
    Assert-Match -Pattern 'PolyHaven\\\\?red_brick' -Actual ($saved | ConvertTo-Json -Compress)
    Assert-Match -Pattern 'red_brick_diff_2k\.png$' -Actual $saved.diffuse_textname
    Assert-NotMatch -Pattern '[A-Za-z]:\\' -Actual $saved.diffuse_textname -Message 'no drive letter in the saved path'
}

Test 're-importing repoints the existing material rather than failing on the name' {
    SendOk 'polyhaven_import red_brick 1k' | Out-Null
    Assert-Match -Pattern '^last=ok red_brick \(1k, 4 maps\) updated in' -Actual (Last (Wait-Import))
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_diff_1k.png' -Actual (Slot red_brick diffuse)
    # Normal, ARM and displacement only exist at 2k here: with nothing smaller, the smallest there is.
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_nor_dx_2k.png' -Actual (Slot red_brick normal)
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_disp_2k.png' -Actual (Slot red_brick height)
    Assert-Equal -Expected 'red_brick' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'Material').name `
        -Message 'the entity using it keeps it'
}

Test 'the height map comes by default, and noheight skips it' {
    # Clear the slot first: an import only ever fills slots, so a height map left over from
    # the imports above would read as "downloaded" whatever this one did.
    SendOk 'set_material_texture red_brick height none' | Out-Null
    SendOk 'polyhaven_import red_brick 2k noheight' | Out-Null
    Assert-Match -Pattern '^last=ok red_brick \(2k, 3 maps\) updated in' -Actual (Last (Wait-Import))
    Assert-Equal -Expected '' -Actual (Slot red_brick height) -Message 'noheight leaves the slot alone'
    SendOk 'polyhaven_import red_brick 2k' | Out-Null
    Assert-Match -Pattern '^last=ok red_brick \(2k, 4 maps\) updated in' -Actual (Last (Wait-Import))
    Assert-Equal -Expected 'PolyHaven\red_brick\red_brick_disp_2k.png' -Actual (Slot red_brick height)
    Assert-Match -Pattern 'height=yes$' -Actual (SendOk 'material_surface red_brick')[0].Text
}

Test 'a missing resolution falls back, and an asset without ARM gets its AO map' {
    SendOk 'polyhaven_import plank_wood 4k' | Out-Null
    Assert-Match -Pattern '^last=ok plank_wood \(4k, 2 maps\) added' -Actual (Last (Wait-Import)) `
        -Message 'this fixture has no displacement map, so the height default adds nothing'
    Assert-Equal -Expected 'PolyHaven\plank_wood\plank_wood_diff_1k.png' -Actual (Slot plank_wood diffuse)
    Assert-Equal -Expected 'PolyHaven\plank_wood\plank_wood_ao_1k.png' -Actual (Slot plank_wood ao)
    Assert-Equal -Expected '' -Actual (Slot plank_wood arm)
}

Test 'a failed import reports why and creates nothing' {
    SendOk 'polyhaven_import old_brick' | Out-Null
    Assert-Match -Pattern '^last=error old_brick has no diffuse map' -Actual (Last (Wait-Import))
    SendOk 'polyhaven_import ghost_asset' | Out-Null
    Assert-Match -Pattern '^last=error cannot read' -Actual (Last (Wait-Import))
    $names = (SendOk 'materials')[0].Payload
    Assert-False -Condition ([bool]($names | Where-Object { $_ -like 'old_brick *' -or $_ -like 'ghost_asset *' })) `
        -Message 'no material for a failed import'
}

Test 'only one import runs at a time' {
    $r = Send 'polyhaven_import plank_wood 1k', 'polyhaven_import red_brick 1k'
    Assert-Ok -Result $r[0]
    Assert-Err -Result $r[1] -Pattern 'already running'
    Wait-Import | Out-Null
}

Test 'the Poly Haven tab draws its grid and previews' {
    # Separate batches: the panel needs a frame to settle before it is worth a shot.
    SendOk 'polyhaven_show' | Out-Null
    Step-EditorFrames -Session $Session -Count 5
    Assert-FileExists -Path (Shot 'polyhaven-tab')
}

Test 'the real API answers in the format the parser expects' {
    SendOk 'polyhaven_source default', 'polyhaven_refresh' | Out-Null
    $s = Wait-Catalog -TimeoutSec 60
    if ($s.Text -match 'catalog=failed') {
        Skip-Test -Reason ("polyhaven.com unreachable: " + (($s.Payload | Where-Object { $_ -like 'catalog_error=*' }) -join ''))
    }
    Assert-Match -Pattern 'catalog=ready' -Actual $s.Text
    $count = [int]([regex]::Match($s.Text, 'assets=(\d+)').Groups[1].Value)
    Assert-True -Condition ($count -gt 100) -Message "the live catalog has $count textures"
    $brick = (SendOk 'polyhaven_list brick')[0]
    Assert-True -Condition ($brick.Payload.Count -gt 0) -Message 'the brick category is populated'
}
