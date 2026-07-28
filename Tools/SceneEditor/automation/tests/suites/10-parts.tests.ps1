# fixture: empty
# description: Composed templates - parts as references, offsets in the root frame, spawn naming, apply-back.

function PartLine {
    param([string]$Template, [string]$Part)
    $r = SendOk "template_parts $Template"
    return ($r[0].Payload | Where-Object { $_ -like "$Part *" })
}

function TemplateLine {
    param([string]$Name)
    $r = SendOk 'list_templates'
    return ($r[0].Payload | Where-Object { $_ -like "$Name *" -or $_ -eq $Name })
}

Test 'a fresh template has no parts' {
    SendOk 'create_template rig' | Out-Null
    $r = SendOk 'template_parts rig'
    Assert-Equal -Expected '0 parts on template rig' -Actual $r[0].Text
}

Test 'template_add_part adds another template at the root origin' {
    $r = SendOk 'template_add_part rig tf_marker'
    Assert-Match -Pattern 'rig \+ \S+ \(tf_marker\)' -Actual $r[0].Text
    $line = PartLine -Template 'rig' -Part 'tf_marker'
    Assert-Match -Pattern 'is=tf_marker' -Actual $line
    Assert-Match -Pattern 'attached_to=root' -Actual $line
    Assert-Match -Pattern 'pos=0,0,0' -Actual $line
}

Test 'a template cannot be composed into itself' {
    Assert-Err -Result (Send 'template_add_part rig rig')[0]
    SendOk 'create_template rig2' | Out-Null
    SendOk 'template_add_part rig2 rig' | Out-Null
    Assert-Err -Result (Send 'template_add_part rig rig2')[0] -Message 'nor through a cycle'
}

Test 'template_add_part rejects unknown templates' {
    Assert-Err -Result (Send 'template_add_part rig nope')[0]
    Assert-Err -Result (Send 'template_add_part nope tf_marker')[0]
}

Test 'template_set_part edits one part as a delta' {
    SendOk "template_set_part rig tf_marker ""{'position':{'x':2.0,'y':1.0,'z':0.0}}""" | Out-Null
    $line = PartLine -Template 'rig' -Part 'tf_marker'
    Assert-Match -Pattern 'pos=2,1,0' -Actual $line
    Assert-Match -Pattern 'attached_to=root' -Actual $line -Message 'a key left out keeps its value'

    SendOk "template_set_part rig tf_marker ""{'scale':{'x':3.0,'y':3.0,'z':3.0}}""" | Out-Null
    $line = PartLine -Template 'rig' -Part 'tf_marker'
    Assert-Match -Pattern 'pos=2,1,0' -Actual $line -Message 'the earlier offset survived'
    Assert-Match -Pattern 'scale=3,3,3' -Actual $line
}

Test 'attach false cuts the part loose without moving it' {
    # An attached part and a detached one at the same offset land in the same
    # place; flipping attach never moves anything.
    SendOk "template_set_part rig tf_marker ""{'attach':false}""" | Out-Null
    $line = PartLine -Template 'rig' -Part 'tf_marker'
    Assert-Match -Pattern 'attached_to=\(free\)' -Actual $line
    Assert-Match -Pattern 'pos=2,1,0' -Actual $line
    SendOk "template_set_part rig tf_marker ""{'attach':true}""" | Out-Null
}

Test 'template_set_part rejects an unknown part and bad JSON' {
    Assert-Err -Result (Send "template_set_part rig nope ""{'attach':false}""")[0] -Pattern 'unknown part'
    Assert-Err -Result (Send "template_set_part rig tf_marker ""{oops}""")[0] -Pattern 'bad JSON'
}

Test 'editing a component block does not decompose the template' {
    # TemplateOps::ApplyComponent builds a snapshot to apply; one built from the
    # components alone silently dropped the parts array (and reset the storage to
    # "in a file"), so a single Transform drag in the panel decomposed the object.
    SendOk 'template_storage rig level' | Out-Null
    SendOk 'template_set rig Transform "{''position'':{''x'':0.0,''y'':0.0,''z'':0.0}}"' | Out-Null
    Assert-Equal -Expected '1 parts on template rig' -Actual (SendOk 'template_parts rig')[0].Text `
        -Message 'the part survived a component edit'
    Assert-Match -Pattern 'in=level' -Actual (TemplateLine -Name 'rig') -Message 'and so did the storage choice'
}

Test 'placing a composed template spawns the root and every part' {
    $placed = Get-PlacedInstance -Result (Send 'place rig')[0]
    $names = Get-EntityNames -Session $Session
    Assert-Contains -Collection $names -Value $placed.Name -Message 'the root'
    # World::InstanceEntityNames owns the rule: <instance>__<part>, recursively.
    Assert-Contains -Collection $names -Value "$($placed.Name)__tf_marker" -Message 'the part'
    $script:composed = $placed.Name
}

Test 'a part is placed at its offset in the root frame' {
    $part = Get-Position -Session $Session -Entity "$($script:composed)__tf_marker"
    Assert-Vector3Near -Expected @{ x = 2.0; y = 1.0; z = 0.0 } -Actual $part -Tolerance 0.01
}

Test 'an attached part carries no rigid body, but the root does' {
    # Bone-attached, a part's pose changes every frame, so a collider is stale the
    # moment it is made; DYNAMIC/KINEMATIC, the physics thread writes body poses
    # into the Transform and undoes the attachment. Both are stripped at spawn, and
    # a composed object that moves carries its collision on the root.
    SendOk 'template_add_component rig Physics' | Out-Null
    SendOk "template_set rig Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null
    SendOk 'template_add_component tf_marker Physics' | Out-Null
    SendOk "template_set tf_marker Physics ""{'type':'DYNAMIC','shape':'BOX'}""" | Out-Null

    $placed = Get-PlacedInstance -Result (Send 'place rig')[0]
    Assert-Contains -Collection (SendOk "components $($placed.Name)")[0].Payload -Value 'Physics' `
        -Message 'the composed object collides through its root'
    Assert-NotContains -Collection (SendOk "components $($placed.Name)__tf_marker")[0].Payload -Value 'Physics' `
        -Message 'components of an attached part'

    SendOk 'template_remove_component tf_marker Physics' | Out-Null
    SendOk 'template_remove_component rig Physics' | Out-Null
}

Test 'the whole composed object deletes and undoes as one' {
    SendOk "select $($script:composed)" | Out-Null
    SendOk 'delete' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-NotContains -Collection $names -Value $script:composed -Message 'root gone'
    Assert-NotContains -Collection $names -Value "$($script:composed)__tf_marker" -Message 'part gone with it'
    SendOk 'undo' | Out-Null
    $names = Get-EntityNames -Session $Session
    Assert-Contains -Collection $names -Value $script:composed -Message 'root back'
    Assert-Contains -Collection $names -Value "$($script:composed)__tf_marker" -Message 'part back'
}

Test 'template_remove_part drops a part' {
    SendOk 'template_remove_part rig2 rig' | Out-Null
    Assert-Equal -Expected '0 parts on template rig2' -Actual (SendOk 'template_parts rig2')[0].Text
    Assert-Err -Result (Send 'template_remove_part rig2 nope')[0]
}

Test 'template_from_selection composes the selection around the first pick' {
    SendOk 'select box_a box_b box_c' | Out-Null
    SendOk 'template_from_selection house' | Out-Null
    $r = SendOk 'template_parts house'
    Assert-Equal -Expected '2 parts on template house' -Actual $r[0].Text `
        -Message 'the root is the template body, the other two are parts'
    $placed = Get-PlacedInstance -Result (Send 'place house')[0]
    $names = Get-EntityNames -Session $Session
    Assert-Equal -Expected 3 -Actual @($names | Where-Object { $_ -like "$($placed.Name)*" }).Count
}

Test 'template_from_selection needs a selection' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'template_from_selection nothing')[0] -Pattern 'nothing selected'
}

Test 'apply_to_template pushes a moved part back into the definition' {
    $placed = Get-PlacedInstance -Result (Send 'place rig')[0]
    $partName = "$($placed.Name)__tf_marker"
    SendOk "select $partName" | Out-Null
    SendOk 'set_position 5 0 0' | Out-Null
    SendOk "apply_to_template $partName" | Out-Null

    # The definition now says what the instance showed...
    $line = PartLine -Template 'rig' -Part 'tf_marker'
    Assert-Match -Pattern 'pos=5,0,0' -Actual $line
    # ...so the next instance is spawned there, and the moved one is no longer an override.
    $next = Get-PlacedInstance -Result (Send 'place rig')[0]
    Assert-Vector3Near -Expected @{ x = 5.0; y = 0.0; z = 0.0 } `
        -Actual (Get-Position -Session $Session -Entity "$($next.Name)__tf_marker") -Tolerance 0.01
}

Test 'apply_to_template needs a target' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'apply_to_template')[0] -Pattern 'usage:'
}

Test 'parts survive a save/reload of the template file' {
    SendOk 'template_storage rig file' | Out-Null
    SendOk 'save_templates' | Out-Null
    $tpl = Join-Path $Assets 'Templates\rig.tpl'
    Assert-FileExists -Path $tpl -Message 'the .tpl'
    $definition = Get-Content $tpl -Raw | ConvertFrom-Json
    Assert-Equal -Expected 1 -Actual @($definition.parts).Count
    Assert-Equal -Expected 'tf_marker' -Actual $definition.parts[0].template
    Assert-Near -Expected 5.0 -Actual $definition.parts[0].position.x -Tolerance 0.001
}
