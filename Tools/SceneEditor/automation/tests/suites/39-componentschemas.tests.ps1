# fixture: empty
# description: Game components described by a JSON schema: discovery, typed add/edit/validation, undo, paste, gizmos, and the level record they save and reload through.

# The schema a game ships. Written into the generated project here, then picked up
# with reload_component_schemas - the editor read the project at startup, before
# this file existed, which is also what proves the reload path works.
function Write-Utf8 {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

$schemaPath = Join-Path $Project 'components.schema.json'
$schemaText = @'
{ "components": [
  { "name": "ResourceNode", "description": "A gatherable node.",
    "fields": {
      "resource": { "type": "enum", "values": ["food", "water", "materials"], "default": "materials" },
      "amount":   { "type": "int", "min": 0, "max": 500, "default": 40 },
      "regrows":  { "type": "bool" } } },
  { "name": "BuildingRef",
    "fields": {
      "building_id": { "type": "enum", "source": "Data/Buildings/*.json#id" },
      "level":       { "type": "int", "min": 1, "default": 1 } } },
  { "name": "Zone",
    "fields": {
      "kind":    { "type": "enum", "values": ["gate", "shelter", "hazard"] },
      "radius":  { "type": "float", "min": 0, "default": 3, "gizmo": "sphere" },
      "extents": { "type": "vec3", "default": { "x": 1, "y": 2, "z": 1 }, "gizmo": "box" },
      "label":   { "type": "string" } } },
  { "name": "Physics", "fields": { "mass": { "type": "float" } } }
] }
'@

Test 'without a schema file there are no schema components' {
    $r = SendOk 'component_schemas'
    Assert-Match -Pattern '^0 schemas from 0 files' -Actual $r[0].Text
    Assert-Err -Result (Send 'add_component box_a ResourceNode') -Pattern 'unknown component'
}

Test 'the project schema is found by its default name and reloaded' {
    Write-Utf8 -Path (Join-Path $Project 'Data\Buildings\farm.json') -Text '{ "id": "farm" }'
    Write-Utf8 -Path (Join-Path $Project 'Data\Buildings\defenses.json') -Text '[ { "id": "wall" }, { "id": "turret" } ]'
    Write-Utf8 -Path $schemaPath -Text $schemaText

    $r = SendOk 'reload_component_schemas'
    Assert-Match -Pattern '^3 schemas from 1 files, 1 problems' -Actual $r[0].Text
    $lines = @($r[0].Payload)
    Assert-True -Condition ([bool]($lines | Where-Object { $_ -match '^ResourceNode fields=3 ' })) -Message 'ResourceNode listed'
    Assert-True -Condition ([bool]($lines | Where-Object { $_ -match '^problem: Physics: is a component the editor already defines' })) `
        -Message 'an engine component name is refused, not shadowed'
}

Test 'an enum can take its values from data files' {
    $r = SendOk 'component_schema BuildingRef'
    $schema = $r[0].Payload[0] | ConvertFrom-Json
    $values = @(($schema.fields | Where-Object { $_.name -eq 'building_id' }).values)
    foreach ($v in @('farm', 'wall', 'turret')) {
        Assert-Contains -Collection $values -Value $v -Message 'values read from Data/Buildings'
    }
}

Test 'adding one gives every field its default' {
    SendOk 'add_component box_a ResourceNode' | Out-Null
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'ResourceNode'
    $v = Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode'
    Assert-Equal -Expected 'materials' -Actual $v.resource
    Assert-Equal -Expected 40 -Actual $v.amount
    Assert-Equal -Expected $false -Actual $v.regrows
    Assert-Err -Result (Send 'add_component box_a ResourceNode') -Pattern 'already has'
}

Test 'edits are checked against the schema' {
    SendOk "set_component box_a ResourceNode {'resource':'water','amount':75,'regrows':true}" | Out-Null
    $v = Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode'
    Assert-Equal -Expected 'water' -Actual $v.resource
    Assert-Equal -Expected 75 -Actual $v.amount
    Assert-Equal -Expected $true -Actual $v.regrows

    # Out of range is clamped, like a clamped drag; wrong values are refused whole.
    SendOk "set_component box_a ResourceNode {'amount':9000}" | Out-Null
    Assert-Equal -Expected 500 -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode').amount
    Assert-Err -Result (Send "set_component box_a ResourceNode {'resource':'gold'}") -Pattern 'not one of'
    Assert-Err -Result (Send "set_component box_a ResourceNode {'amount':'lots'}") -Pattern 'must be a number'
    Assert-Err -Result (Send "set_component box_a ResourceNode {'colour':1}") -Pattern "has no field 'colour'"
    Assert-Equal -Expected 'water' -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode').resource `
        -Message 'a refused edit changes nothing'
}

Test 'edits, adds and removes are undoable, and a removal restores its values' {
    SendOk "set_component box_a ResourceNode {'amount':120}" | Out-Null
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected 500 -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode').amount -Message 'after undo'
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected 120 -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode').amount -Message 'after redo'

    SendOk 'remove_component box_a ResourceNode' | Out-Null
    Assert-NotContains -Collection (SendOk 'components box_a')[0].Payload -Value 'ResourceNode' -Message 'after remove'
    SendOk 'undo' | Out-Null
    $v = Get-Component -Session $Session -Entity 'box_a' -Component 'ResourceNode'
    Assert-Equal -Expected 120 -Actual $v.amount -Message 'undo brings back the values, not defaults'
    Assert-Equal -Expected 'water' -Actual $v.resource
}

Test 'a gizmo field draws around the selected entity' {
    SendOk 'add_component box_b Zone', 'select box_b' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $r = SendOk 'schema_gizmo_info'
    # One sphere (radius 3: three 40-segment circles) and one box (12 edges).
    Assert-Match -Pattern '^spheres=1 boxes=1 segments=132$' -Actual $r[0].Text

    SendOk "set_component box_b Zone {'radius':0}" | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    Assert-Match -Pattern '^spheres=0 boxes=1 ' -Actual (SendOk 'schema_gizmo_info')[0].Text -Message 'a zero radius draws nothing'

    SendOk 'deselect' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    Assert-Match -Pattern '^spheres=0 boxes=0 segments=0$' -Actual (SendOk 'schema_gizmo_info')[0].Text -Message 'selection only'
}

Test 'a pasted copy carries its game components' {
    SendOk 'add_component box_c BuildingRef', "set_component box_c BuildingRef {'building_id':'wall','level':2}" | Out-Null
    $before = Get-EntityNames -Session $Session
    SendOk 'select box_c', 'copy', 'paste' | Out-Null
    $new = @((Get-EntityNames -Session $Session) | Where-Object { $before -notcontains $_ })
    Assert-Equal -Expected 1 -Actual $new.Count -Message 'one copy appeared'
    $copy = $new[0]
    $v = Get-Component -Session $Session -Entity $copy -Component 'BuildingRef'
    Assert-Equal -Expected 'wall' -Actual $v.building_id
    Assert-Equal -Expected 2 -Actual $v.level
    SendOk 'undo' | Out-Null
    Assert-NotContains -Collection (Get-EntityNames -Session $Session) -Value $copy -Message 'after undoing the paste'
}

Test 'a save writes the blocks into the entity records, and a removal as "remove"' {
    SendOk 'add_component box_c ResourceNode', 'remove_component box_c ResourceNode' | Out-Null
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $a = $level.world.instances | Where-Object { $_.name -eq 'box_a' }
    Assert-Equal -Expected 'water' -Actual $a.components.ResourceNode.resource
    Assert-Equal -Expected 120 -Actual $a.components.ResourceNode.amount
    $c = $level.world.instances | Where-Object { $_.name -eq 'box_c' }
    Assert-Equal -Expected 'wall' -Actual $c.components.BuildingRef.building_id
    Assert-True -Condition ($null -eq $c.components.ResourceNode) -Message 'a removed block is not written'
    Assert-Contains -Collection @($c.remove) -Value 'ResourceNode' -Message 'the removal is recorded'
}

Test 'a reloaded level edits them straight away' {
    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-schemas'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $v = Get-Component -Session $reloaded -Entity 'box_a' -Component 'ResourceNode'
        Assert-Equal -Expected 'water' -Actual $v.resource
        $r = Invoke-EditorCommand -Session $reloaded -Command "set_component box_a ResourceNode {'amount':7}"
        Assert-Ok -Result $r[0]
        Assert-Equal -Expected 7 -Actual (Get-Component -Session $reloaded -Entity 'box_a' -Component 'ResourceNode').amount
        Assert-Equal -Expected 'wall' -Actual (Get-Component -Session $reloaded -Entity 'box_c' -Component 'BuildingRef').building_id
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'config.json can name the schema files instead' {
    $configPath = Join-Path $Project 'config.json'
    $config = Get-Content $configPath -Raw | ConvertFrom-Json
    $config | Add-Member -NotePropertyName editor -NotePropertyValue @{ component_schemas = @('Schemas\only_zone.json') } -Force
    Write-Utf8 -Path $configPath -Text ($config | ConvertTo-Json -Depth 10)
    Write-Utf8 -Path (Join-Path $Project 'Schemas\only_zone.json') -Text `
        '{ "components": [ { "name": "SpawnMarker", "fields": { "faction": { "type": "string" } } } ] }'

    $r = SendOk 'reload_component_schemas'
    Assert-Match -Pattern '^1 schemas from 1 files, 0 problems' -Actual $r[0].Text
    Assert-Match -Pattern '^SpawnMarker ' -Actual $r[0].Payload[0]
    # The default file is no longer read, so what it described is opaque again:
    # still listed and still saved, but no longer editable.
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'ResourceNode' -Message 'kept'
    Assert-Err -Result (Send "set_component box_a ResourceNode {'amount':1}")
}
