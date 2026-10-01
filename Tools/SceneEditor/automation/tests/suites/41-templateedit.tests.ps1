# fixture: empty
# description: Template edit mode - a template edited directly, alone in the viewport, with the ordinary tools, and written back to its .tpl; opening a .tpl file for editing; the level is never touched by it.

function Write-Utf8 {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

# The game component the template will carry, described the way a game ships it.
Write-Utf8 -Path (Join-Path $Project 'components.schema.json') -Text @'
{ "components": [
  { "name": "NightLight",
    "fields": { "fade_minutes": { "type": "float", "min": 0, "max": 240, "default": 30 } } }
] }
'@

function Get-EditorState {
    $r = SendOk 'state'
    return ($r[0].Text | ConvertFrom-Json)
}

function Get-EntityNames {
    $r = SendOk 'list_entities'
    return @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
}

function Get-Visible {
    param([string]$Entity)
    $r = SendOk "component $Entity Base"
    return [bool](($r[0].Payload -join '') | ConvertFrom-Json).visible
}

Test 'a template with a part, a bystander in the level, and the schema are ready' {
    SendOk 'reload_component_schemas' | Out-Null
    SendOk 'create_template lamp', 'template_add_component lamp PointLight' | Out-Null
    SendOk 'create_template hut', 'template_add_part hut lamp' | Out-Null
    SendOk 'create_template bystander' | Out-Null
    $by = Get-PlacedInstance -Result (Send 'place bystander')[0]
    $script:bystander = $by.Name
    Assert-Contains -Collection (Get-EntityNames) -Value $script:bystander
}

Test 'edit_template puts the template alone at the origin, with its parts, and hides the rest' {
    $r = SendOk 'edit_template hut'
    Assert-Match -Pattern 'editing hut as hut_editing' -Actual $r[0].Text
    Assert-Equal -Expected 'hut' -Actual (Get-EditorState).template_edit
    $names = Get-EntityNames
    Assert-Contains -Collection $names -Value 'hut_editing'
    Assert-Contains -Collection $names -Value 'hut_editing__lamp'
    Assert-True -Condition (Get-Visible -Entity 'hut_editing') -Message 'the template is shown'
    Assert-NotContains -Collection $names -Value $script:bystander -Message 'the level is not in the Entities list'
    Assert-False -Condition (Get-Visible -Entity $script:bystander) -Message 'and is hidden from the scene'
    foreach ($listed in $names) {
        Assert-Match -Pattern '^hut_editing' -Actual $listed -Message 'only the template is listed'
    }
    Assert-Equal -Expected 'hut_editing' -Actual (Get-EditorState).selected_entity_name -Message 'and selected'
}

Test 'a second session is refused, and so is saving the level over it' {
    Assert-Err -Result (Send 'edit_template lamp')[0] -Pattern 'already editing'
    $r = SendOk 'menu "File/Save Level"'
    Assert-Match -Pattern 'Close the template' -Actual $r[0].Text
}

Test 'an unknown or non-authored template is refused' {
    SendOk 'end_template_edit' | Out-Null
    Assert-Err -Result (Send 'edit_template nope')[0] -Pattern 'not an authored template'
    Assert-Err -Result (Send 'end_template_edit')[0] -Pattern 'not editing'
    Assert-Err -Result (Send 'save_template_edit')[0] -Pattern 'not editing'
}

Test 'closing without saving gives the level back and keeps the template as it was' {
    SendOk 'edit_template hut' | Out-Null
    SendOk 'select hut_editing__lamp', 'set_position 0 3 0' | Out-Null
    SendOk 'add_component hut_editing NightLight' | Out-Null
    SendOk 'end_template_edit' | Out-Null
    Assert-Equal -Expected '' -Actual (Get-EditorState).template_edit
    $names = Get-EntityNames
    Assert-NotContains -Collection $names -Value 'hut_editing'
    Assert-NotContains -Collection $names -Value 'hut_editing__lamp'
    Assert-Contains -Collection $names -Value $script:bystander -Message 'the level is listed again'
    Assert-True -Condition (Get-Visible -Entity $script:bystander) -Message 'the bystander is shown again'
    Assert-Match -Pattern 'pos=0,0,0' -Actual ((SendOk 'template_parts hut')[0].Payload | Select-Object -First 1)
    Assert-NotMatch -Pattern 'NightLight' -Actual ((SendOk 'template_info hut')[0].Payload -join "`n")
}

Test 'what is edited is written back: a moved part, and game components on the root and on a part' {
    SendOk 'edit_template hut' | Out-Null
    SendOk 'select hut_editing__lamp', 'set_position 0 3 0' | Out-Null
    SendOk 'add_component hut_editing NightLight' | Out-Null
    SendOk "set_component hut_editing NightLight ""{'fade_minutes':12}""" | Out-Null
    SendOk 'add_component hut_editing__lamp NightLight' | Out-Null
    SendOk "set_component hut_editing__lamp NightLight ""{'fade_minutes':45}""" | Out-Null
    $r = SendOk 'save_template_edit'
    Assert-Match -Pattern "Saved template 'hut'" -Actual $r[0].Text

    $part = (SendOk 'template_parts hut')[0].Payload | Select-Object -First 1
    Assert-NotMatch -Pattern 'pos=0,0,0' -Actual $part -Message 'the part moved in the template'
    $info = (SendOk 'template_info hut')[0].Payload -join "`n"
    Assert-Match -Pattern 'NightLight \{"fade_minutes":12' -Actual $info -Message 'the root carries its game component'

    $tpl = Get-Content (Join-Path $Assets 'Templates\hut.tpl') -Raw | ConvertFrom-Json
    Assert-Near -Expected 12 -Actual $tpl.components.NightLight.fade_minutes -Tolerance 0.001 -Message 'in the file'
    Assert-Near -Expected 45 -Actual $tpl.parts[0].components.NightLight.fade_minutes -Tolerance 0.001 -Message 'and the part in the file, as an override on that part'
    # The shared part template itself is not changed under its other users.
    Assert-NotMatch -Pattern 'NightLight' -Actual ((SendOk 'template_info lamp')[0].Payload -join "`n")
}

Test 'the session goes on after a save, and ends cleanly' {
    Assert-Equal -Expected 'hut' -Actual (Get-EditorState).template_edit
    SendOk 'end_template_edit' | Out-Null
    Assert-Equal -Expected '' -Actual (Get-EditorState).template_edit
    $placed = Get-PlacedInstance -Result (Send 'place hut')[0]
    $names = Get-EntityNames
    Assert-Contains -Collection $names -Value "$($placed.Name)__lamp" -Message 'a fresh instance has the part'
}

Test 'the level never sees the session: saved, it holds no session instance and nothing is hidden' {
    SendOk 'edit_template hut' | Out-Null
    SendOk 'end_template_edit' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw
    Assert-NotMatch -Pattern 'hut_editing' -Actual $level
    $doc = $level | ConvertFrom-Json
    foreach ($entity in @($doc.world.entities)) {
        if ($entity.name -eq $script:bystander) {
            Assert-True -Condition ($entity.visible -ne $false) -Message 'the bystander was not saved hidden'
        }
    }
}

Test 'open_template opens a .tpl the project already has, in place' {
    $r = SendOk "open_template $(Join-Path $Assets 'Templates\hut.tpl')"
    Assert-Match -Pattern 'editing hut as hut_editing' -Actual $r[0].Text
    SendOk 'end_template_edit' | Out-Null
}

Test 'open_template imports a .tpl from elsewhere and edits it' {
    $outside = Join-Path $WorkDir 'elsewhere\shed.tpl'
    Write-Utf8 -Path $outside -Text '{ "name": "shed", "components": { "Transform": { "position": {"x":0,"y":0,"z":0}, "rotation": {"x":0,"y":0,"z":0,"w":1}, "scale": {"x":1,"y":1,"z":1} } } }'
    $r = SendOk "open_template $outside"
    Assert-Match -Pattern 'editing shed as shed_editing' -Actual $r[0].Text
    Assert-FileExists -Path (Join-Path $Assets 'Templates\shed.tpl') -Message 'brought into the project'
    SendOk 'add_component shed_editing NightLight' | Out-Null
    SendOk 'end_template_edit save' | Out-Null
    $tpl = Get-Content (Join-Path $Assets 'Templates\shed.tpl') -Raw | ConvertFrom-Json
    Assert-Near -Expected 30 -Actual $tpl.components.NightLight.fade_minutes -Tolerance 0.001 -Message 'saved into the imported template'
    Assert-Err -Result (Send 'open_template C:\no\such\file.tpl')[0] -Pattern 'could not read'
}

Test 'the view needs nothing from the level: a camera and an ambient of its own, not listed, gone afterwards' {
    SendOk 'create_template cabin', 'template_add_part cabin lamp' | Out-Null
    SendOk 'edit_template cabin' | Out-Null
    SendOk 'camera' | Out-Null
    foreach ($listed in (Get-EntityNames)) {
        Assert-NotMatch -Pattern '^__template_edit' -Actual $listed -Message 'the session view is not listed'
    }
    Assert-Equal -Expected 'cabin_editing' -Actual (Get-EditorState).selected_entity_name
    SendOk 'end_template_edit' | Out-Null
    Assert-Err -Result (Send 'component __template_edit_ambient Base')[0] -Pattern '.' -Message 'the session ambient is gone'
    Assert-Err -Result (Send 'component __template_edit_camera Base')[0] -Pattern '.' -Message 'and so is the session camera'
}

Test 'an entity added to the template and dragged onto the root becomes a part when it is saved' {
    SendOk 'edit_template cabin' | Out-Null
    $r = SendOk 'menu "Add/Point Light"'
    $light = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    Assert-Contains -Collection (Get-EntityNames) -Value $light -Message 'an entity added in the session is listed'
    SendOk "select $light", 'set_position 1 2 3' | Out-Null
    SendOk "set_parent $light cabin_editing" | Out-Null
    Assert-Match -Pattern 'parent=cabin_editing' -Actual ((SendOk 'list_entities')[0].Payload | Where-Object { $_ -like "$light *" })
    SendOk 'save_template_edit' | Out-Null

    $parts = (SendOk 'template_parts cabin')[0].Payload
    Assert-Equal -Expected 2 -Actual @($parts).Count -Message 'the lamp it had and the new light'
    $line = $parts | Where-Object { $_ -like "$light *" }
    Assert-Match -Pattern 'attached_to=root' -Actual $line -Message 'a child of the root is attached to it'
    # It lives inside the template's own file: no template of its own is made for it.
    $templateNames = (SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] }
    Assert-NotContains -Collection $templateNames -Value $light -Message 'no separate template'
    Assert-True -Condition (-not (Test-Path (Join-Path $Assets "Templates\$light.tpl"))) -Message 'and no separate file'
    SendOk 'save_templates' | Out-Null
    $saved = Get-Content (Join-Path $Assets 'Templates\cabin.tpl') -Raw | ConvertFrom-Json
    $inline = $saved.parts | Where-Object { $_.name -eq $light }
    Assert-True -Condition ($null -ne $inline) -Message 'the part is in the cabin file'
    Assert-True -Condition ($null -eq $inline.template) -Message 'defined inline, not by reference'
    Assert-True -Condition ($null -ne $inline.components.PointLight) -Message 'with its own components'
    Assert-Near -Expected 1 -Actual $inline.position.x -Tolerance 0.001 -Message 'at the offset it was given'
    Assert-Near -Expected 3 -Actual $inline.position.z -Tolerance 0.001
    # The session shows the template as saved: the new part is a child of the root now.
    Assert-Match -Pattern 'parent=cabin_editing' -Actual ((SendOk 'list_entities')[0].Payload | Where-Object { $_ -like "cabin_editing__$light *" })
    SendOk 'end_template_edit' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames) -Value $light -Message 'nothing the session made is left in the level'
    $placed = Get-PlacedInstance -Result (Send 'place cabin')[0]
    $p = Get-Position -Session $Session -Entity "$($placed.Name)__$light"
    Assert-Near -Expected 1 -Actual $p.x -Tolerance 0.001 -Message 'a placed cabin spawns the inline part where it was put'
    Assert-Near -Expected 2 -Actual $p.y -Tolerance 0.001
    Assert-Near -Expected 3 -Actual $p.z -Tolerance 0.001
}

Test 'an entity let go of its parent is saved as a free part, and a deleted one is gone from the template' {
    SendOk 'edit_template cabin' | Out-Null
    SendOk 'set_parent cabin_editing__lamp none' | Out-Null
    SendOk 'save_template_edit' | Out-Null
    Assert-Match -Pattern 'attached_to=\(free\)' -Actual ((SendOk 'template_parts cabin')[0].Payload | Where-Object { $_ -like 'lamp *' })

    SendOk 'select cabin_editing__lamp', 'delete' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames) -Value 'cabin_editing__lamp'
    Assert-Contains -Collection (Get-EntityNames) -Value 'cabin_editing' -Message 'the root is still there'
    SendOk 'save_template_edit' | Out-Null
    $left = (SendOk 'template_parts cabin')[0].Payload
    Assert-NotMatch -Pattern '^lamp ' -Actual ($left -join "`n") -Message 'the deleted part is gone from the template'
    SendOk 'end_template_edit' | Out-Null
}

Test 'the root of the template cannot be deleted or made a child' {
    SendOk 'edit_template cabin' | Out-Null
    SendOk 'select cabin_editing' | Out-Null
    Assert-Err -Result (Send 'delete')[0] -Pattern 'root cannot be deleted'
    $r = SendOk 'menu "Add/Point Light"'
    $light = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    Assert-Err -Result (Send "set_parent cabin_editing $light")[0] -Pattern 'root cannot be made a child'
    SendOk 'end_template_edit' | Out-Null
}

Test 'Open Template is available with no level open, and opens the project level that lists the template' {
    if ((Split-Path -Parent $LevelPath) -notlike '*\Assets\Levels*') { Skip-Test "the fixture's level is not under Assets\Levels, which is where a template's level is looked for" }
    SendOk 'menu "File/Save Level"', 'menu "File/Close Level"' | Out-Null
    Assert-False -Condition (Get-EditorState).level_loaded -Message 'the level is closed'
    $menus = (SendOk 'menus')[0].Payload
    $line = $menus | Where-Object { $_ -like 'File/Open Template*' }
    Assert-NotMatch -Pattern 'disabled' -Actual $line -Message 'Open Template is not greyed out'

    $r = SendOk "open_template $(Join-Path $Assets 'Templates\hut.tpl')"
    Assert-Match -Pattern 'editing hut as hut_editing' -Actual $r[0].Text
    Assert-True -Condition (Get-EditorState).level_loaded -Message 'its level was opened'
    SendOk 'end_template_edit' | Out-Null
    SendOk 'menu "File/Close Level"' | Out-Null
    Assert-Err -Result (Send "open_template $(Join-Path $WorkDir 'elsewhere\shed.tpl')")[0] -Pattern 'no level in this template'
}
