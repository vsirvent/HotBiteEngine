# fixture: empty
# description: Entities added to a placed instance (a light parented to it) - listed as its children, taken into the template as parts when the instance is applied to it or saved as a new template, removed from the level so nothing spawns twice, undone as one step, and kept through a reload.

function Get-Names {
    $r = SendOk 'list_entities'
    return @($r[0].Payload | ForEach-Object { ($_ -split ' ')[0] })
}

function Get-Line {
    param([string]$Name)
    return ((SendOk 'list_entities')[0].Payload | Where-Object { ($_ -split ' ')[0] -eq $Name })
}

# A point light added to the level at a position, parented to `$Parent`.
function New-ChildLight {
    param([string]$Parent, [string]$Position)
    $r = SendOk 'menu "Add/Point Light"'
    $light = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    SendOk "select $light", "set_position $Position" | Out-Null
    SendOk "set_parent $light $Parent" | Out-Null
    return $light
}

function PartLine {
    param([string]$Template, [string]$Part)
    return ((SendOk "template_parts $Template")[0].Payload | Where-Object { $_ -like "$Part *" })
}

function PartCount {
    param([string]$Template)
    return [int](((SendOk "template_parts $Template")[0].Text -split ' ')[0])
}

Test 'a light parented to a placed instance is one of its child entities' {
    SendOk 'create_template hut' | Out-Null
    $script:hut = (Get-PlacedInstance -Result (Send 'place hut')[0]).Name
    $script:light = New-ChildLight -Parent $script:hut -Position '1 2 3'
    $r = SendOk "instance_children $script:hut"
    Assert-Equal -Expected "1 child entities on $script:hut" -Actual $r[0].Text
    Assert-Equal -Expected $script:light -Actual $r[0].Payload[0]
    # Asked of the child itself, it resolves to the instance it hangs off.
    Assert-Equal -Expected "1 child entities on $script:hut" -Actual (SendOk "instance_children $script:light")[0].Text
}

Test 'what the template spawns is not a child entity, and a free entity is not either' {
    SendOk 'create_template bolt' | Out-Null
    SendOk 'create_template rig', 'template_add_part rig bolt' | Out-Null
    $rig = (Get-PlacedInstance -Result (Send 'place rig')[0]).Name
    Assert-Equal -Expected "0 child entities on $rig" -Actual (SendOk "instance_children $rig")[0].Text -Message 'its own part is not loose'
    $r = SendOk 'menu "Add/Point Light"'
    $free = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    Assert-Equal -Expected "0 child entities on $rig" -Actual (SendOk "instance_children $rig")[0].Text -Message 'nor is a light that hangs off nothing'
    Assert-Err -Result (Send "instance_children $free")[0] -Pattern 'not a placed instance'
}

Test 'the menu offers Apply and Save as New for a selected child, and nothing for a free entity' {
    SendOk "select $script:light" | Out-Null
    $menus = (SendOk 'menus')[0].Payload
    Assert-NotMatch -Pattern 'disabled' -Actual ($menus | Where-Object { $_ -like 'Edit/Apply Instance to Template*' })
    Assert-NotMatch -Pattern 'disabled' -Actual ($menus | Where-Object { $_ -like 'Edit/Save Instance as New Template*' })
    SendOk 'deselect' | Out-Null
    $menus = (SendOk 'menus')[0].Payload
    Assert-Match -Pattern 'disabled' -Actual ($menus | Where-Object { $_ -like 'Edit/Save Instance as New Template*' })
}

Test 'applying the instance takes the child in as an inline part and removes it from the level' {
    SendOk "select $script:light" | Out-Null
    $r = SendOk 'apply_to_template'
    Assert-Match -Pattern '1 child entity\(ies\) as new parts' -Actual $r[0].Text

    Assert-Equal -Expected 1 -Actual (PartCount -Template 'hut')
    $line = PartLine -Template 'hut' -Part $script:light
    Assert-Match -Pattern 'is=\(inline\)' -Actual $line -Message 'defined in the template, not a template of its own'
    Assert-Match -Pattern 'attached_to=root' -Actual $line
    Assert-Match -Pattern 'pos=1,2,3' -Actual $line -Message 'where it stood'
    $templates = (SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] }
    Assert-NotContains -Collection $templates -Value $script:light -Message 'no template is made for the light'

    # The level has the instance, with the light as its own part - and no second light.
    $names = Get-Names
    Assert-Contains -Collection $names -Value $script:hut -Message 'the instance is still there, under its name'
    Assert-NotContains -Collection $names -Value $script:light -Message 'the loose light is gone'
    Assert-Contains -Collection $names -Value "$($script:hut)__$($script:light)" -Message 'it is the instance part now'
    Assert-Match -Pattern "parent=$script:hut" -Actual (Get-Line -Name "$($script:hut)__$($script:light)")
    $p = Get-Position -Session $Session -Entity "$($script:hut)__$($script:light)"
    Assert-Near -Expected 1 -Actual $p.x -Tolerance 0.001
    Assert-Near -Expected 2 -Actual $p.y -Tolerance 0.001
    Assert-Near -Expected 3 -Actual $p.z -Tolerance 0.001
    Assert-Contains -Collection (SendOk "components $($script:hut)__$($script:light)")[0].Payload -Value 'PointLight' `
        -Message 'with its components'
    Assert-Equal -Expected "0 child entities on $script:hut" -Actual (SendOk "instance_children $script:hut")[0].Text
}

Test 'the whole apply is one undo step, and redo does it again' {
    SendOk 'undo' | Out-Null
    Assert-Equal -Expected 0 -Actual (PartCount -Template 'hut') -Message 'the template is as it was'
    $names = Get-Names
    Assert-Contains -Collection $names -Value $script:hut
    Assert-Contains -Collection $names -Value $script:light -Message 'the loose light is back'
    Assert-NotContains -Collection $names -Value "$($script:hut)__$($script:light)" -Message 'and the part is not'
    Assert-Match -Pattern "parent=$script:hut" -Actual (Get-Line -Name $script:light) -Message 'still a child of the instance'
    Assert-Equal -Expected "1 child entities on $script:hut" -Actual (SendOk "instance_children $script:hut")[0].Text

    SendOk 'redo' | Out-Null
    Assert-Equal -Expected 1 -Actual (PartCount -Template 'hut')
    $names = Get-Names
    Assert-NotContains -Collection $names -Value $script:light
    Assert-Contains -Collection $names -Value "$($script:hut)__$($script:light)"
}

Test 'a template placed afterwards has the light, and the template file holds it inline' {
    $next = (Get-PlacedInstance -Result (Send 'place hut')[0]).Name
    Assert-Contains -Collection (Get-Names) -Value "$($next)__$($script:light)" -Message 'a new instance spawns the light'
    $p = Get-Position -Session $Session -Entity "$($next)__$($script:light)"
    Assert-Near -Expected 3 -Actual $p.z -Tolerance 0.001

    SendOk 'save_templates' | Out-Null
    $tpl = Get-Content (Join-Path $Assets 'Templates\hut.tpl') -Raw | ConvertFrom-Json
    $part = @($tpl.parts)[0]
    Assert-Equal -Expected $script:light -Actual $part.name
    Assert-True -Condition ($null -eq $part.template) -Message 'no template named: it is defined inline'
    Assert-True -Condition ($null -ne $part.components.PointLight) -Message 'with the light in it'
}

Test 'a saved level lists the instance once and no loose light, and reloads with exactly one' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $doc = Get-Content $LevelPath -Raw | ConvertFrom-Json
    $created = @($doc.world.created_entities | ForEach-Object { $_.name })
    Assert-NotContains -Collection $created -Value $script:light -Message 'the light is not also a level entity'
    SendOk "open_level $LevelPath" | Out-Null
    $names = Get-Names
    Assert-Contains -Collection $names -Value "$($script:hut)__$($script:light)"
    Assert-NotContains -Collection $names -Value $script:light
    Assert-Equal -Expected 1 -Actual @($names | Where-Object { $_ -like "$($script:hut)__*" }).Count -Message 'one part, spawned once'
}

Test 'an edit made to the instance itself survives the apply' {
    SendOk 'create_template barn' | Out-Null
    $barn = (Get-PlacedInstance -Result (Send 'place barn')[0]).Name
    SendOk "add_component $barn PointLight" | Out-Null
    $light = New-ChildLight -Parent $barn -Position '0 5 0'
    SendOk "apply_to_template $barn" | Out-Null
    Assert-Contains -Collection (Get-Names) -Value "$($barn)__$light"
    Assert-Contains -Collection (SendOk "components $barn")[0].Payload -Value 'PointLight' `
        -Message 'the instance kept the component it was given'
}

Test 'a child placed from a template becomes a part that references it' {
    SendOk 'create_template post', 'create_template bulb' | Out-Null
    $post = (Get-PlacedInstance -Result (Send 'place post')[0]).Name
    $bulb = (Get-PlacedInstance -Result (Send 'place bulb')[0]).Name
    SendOk "select $bulb", 'set_position 0 4 0' | Out-Null
    SendOk "set_parent $bulb $post" | Out-Null
    SendOk "apply_to_template $post" | Out-Null
    $line = PartLine -Template 'post' -Part $bulb
    Assert-Match -Pattern 'is=bulb ' -Actual $line -Message 'a reference to the template it was placed from'
    Assert-Match -Pattern 'pos=0,4,0' -Actual $line
    $names = Get-Names
    Assert-NotContains -Collection $names -Value $bulb -Message 'the loose instance is gone'
    Assert-Contains -Collection $names -Value "$($post)__$bulb"
    # One level in, the same again from the other side: undo puts the child instance back under its parent.
    SendOk 'undo' | Out-Null
    Assert-Contains -Collection (Get-Names) -Value $bulb
    Assert-Match -Pattern "parent=$post" -Actual (Get-Line -Name $bulb) -Message 'with its parent link'
}

Test 'Save as New makes a template of everything and leaves the original alone' {
    SendOk 'create_template lamp', 'template_add_component lamp PointLight' | Out-Null
    SendOk 'create_template cabin', 'template_add_part cabin lamp' | Out-Null
    $cabin = (Get-PlacedInstance -Result (Send 'place cabin')[0]).Name
    SendOk "select $($cabin)__lamp", 'set_position 0 4 0' | Out-Null
    $light = New-ChildLight -Parent $cabin -Position '2 0 2'

    $r = SendOk "save_instance_as_template cabin_lit $cabin"
    Assert-Match -Pattern 'Saved .* as new template cabin_lit \(2 part\(s\), 1 from child entities\)' -Actual $r[0].Text

    Assert-Equal -Expected 2 -Actual (PartCount -Template 'cabin_lit') -Message 'the part it had, and the light'
    Assert-Match -Pattern 'pos=0,4,0' -Actual (PartLine -Template 'cabin_lit' -Part 'lamp') -Message 'the part where it was moved to'
    Assert-Match -Pattern 'is=\(inline\)' -Actual (PartLine -Template 'cabin_lit' -Part $light)
    Assert-Equal -Expected 1 -Actual (PartCount -Template 'cabin') -Message 'the original still has one part'
    Assert-Match -Pattern 'pos=0,0,0' -Actual (PartLine -Template 'cabin' -Part 'lamp') -Message 'and has not moved it'

    $names = Get-Names
    Assert-Contains -Collection $names -Value $cabin -Message 'the instance carries on, as an instance of the new template'
    Assert-Contains -Collection $names -Value "$($cabin)__lamp"
    Assert-Contains -Collection $names -Value "$($cabin)__$light"
    Assert-NotContains -Collection $names -Value $light
    Assert-Near -Expected 4 -Actual (Get-Position -Session $Session -Entity "$($cabin)__lamp").y -Tolerance 0.001

    # Saved, the new template is its own file and the original's is as it was.
    SendOk 'save_templates' | Out-Null
    Assert-FileExists -Path (Join-Path $Assets 'Templates\cabin_lit.tpl')
    Assert-Equal -Expected 1 -Actual @((Get-Content (Join-Path $Assets 'Templates\cabin.tpl') -Raw | ConvertFrom-Json).parts).Count
    $script:cabin = $cabin
}

Test 'Save as New is one undo step: the new template goes and the old instance comes back' {
    SendOk 'undo' | Out-Null
    $templates = (SendOk 'list_templates')[0].Payload | ForEach-Object { ($_ -split ' ')[0] }
    Assert-NotContains -Collection $templates -Value 'cabin_lit' -Message 'no new template'
    $names = Get-Names
    Assert-Contains -Collection $names -Value $script:cabin
    Assert-Contains -Collection $names -Value "$($script:cabin)__lamp"
    Assert-Equal -Expected 1 -Actual @($names | Where-Object { $_ -like "$($script:cabin)__*" }).Count `
        -Message 'only the part the old template has - the light is not one of its parts'
    Assert-Equal -Expected "1 child entities on $script:cabin" -Actual (SendOk "instance_children $script:cabin")[0].Text -Message 'it is a loose child again'
    SendOk 'redo' | Out-Null
    Assert-Equal -Expected 2 -Actual (PartCount -Template 'cabin_lit')
}

Test 'Save as New refuses a taken name, a missing name, and something that is not an instance' {
    Assert-Err -Result (Send "save_instance_as_template cabin $script:cabin")[0] -Pattern 'already exists'
    Assert-Err -Result (Send 'save_instance_as_template')[0] -Pattern 'usage:'
    $r = SendOk 'menu "Add/Point Light"'
    $free = ($r[0].Text -replace '^Created Point Light:\s*', '').Trim()
    Assert-Err -Result (Send "save_instance_as_template nope $free")[0] -Pattern 'not a placed instance'
}

Test 'an instance with no parts and no children has nothing to apply or save' {
    SendOk 'create_template plain' | Out-Null
    $plain = (Get-PlacedInstance -Result (Send 'place plain')[0]).Name
    Assert-Err -Result (Send "apply_to_template $plain")[0] -Pattern 'no parts to apply'
    Assert-Err -Result (Send "save_instance_as_template plain2 $plain")[0] -Pattern 'no parts or child entities'
}

Test 'a template that would contain itself is refused before anything leaves the level' {
    SendOk 'create_template tower' | Out-Null
    $tower = (Get-PlacedInstance -Result (Send 'place tower')[0]).Name
    $inner = (Get-PlacedInstance -Result (Send 'place tower')[0]).Name
    SendOk "set_parent $inner $tower" | Out-Null
    Assert-Err -Result (Send "apply_to_template $tower")[0] -Pattern 'tower'
    $names = Get-Names
    Assert-Contains -Collection $names -Value $inner -Message 'the child is still in the level'
    Assert-Match -Pattern "parent=$tower" -Actual (Get-Line -Name $inner) -Message 'under its parent'
    Assert-Equal -Expected 0 -Actual (PartCount -Template 'tower') -Message 'and the template is untouched'
}

Test 'deleting an instance takes the entities hanging off it, and undo puts both back linked' {
    # A light left behind would hang off an entity that no longer exists, and a light reads
    # its parent's Transform every frame - the editor went down on the next frame.
    SendOk 'create_template shack' | Out-Null
    $shack = (Get-PlacedInstance -Result (Send 'place shack')[0]).Name
    $light = New-ChildLight -Parent $shack -Position '0 2 0'
    SendOk "select $shack", 'delete' | Out-Null
    $names = Get-Names
    Assert-NotContains -Collection $names -Value $shack
    Assert-NotContains -Collection $names -Value $light -Message 'the child went with its parent'
    SendOk 'ping' | Out-Null   # still alive, a frame later
    SendOk 'undo' | Out-Null
    $names = Get-Names
    Assert-Contains -Collection $names -Value $shack
    Assert-Contains -Collection $names -Value $light
    Assert-Match -Pattern "parent=$shack" -Actual (Get-Line -Name $light) -Message 'and it is under the instance again'
    Assert-Near -Expected 2 -Actual (Get-Position -Session $Session -Entity $light).y -Tolerance 0.001
}

Test 'Duplicate keeps the parts of a composed template' {
    SendOk 'duplicate_template cabin_lit cabin_lit2' | Out-Null
    Assert-Equal -Expected 2 -Actual (PartCount -Template 'cabin_lit2')
    Assert-Match -Pattern 'is=\(inline\)' -Actual ((SendOk 'template_parts cabin_lit2')[0].Payload -join "`n") -Message 'inline parts come with it'
    $copy = (Get-PlacedInstance -Result (Send 'place cabin_lit2')[0]).Name
    Assert-Equal -Expected 3 -Actual @((Get-Names) | Where-Object { $_ -like "$copy*" }).Count -Message 'a copy spawns whole'
}
