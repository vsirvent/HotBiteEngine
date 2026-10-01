# fixture: empty
# description: Parent and child - what dragging an entity onto another in the Entities panel does (set_parent): the child keeps its place in the world and its transform becomes relative to the parent; the hierarchy is one level deep; all of it undoable.

function New-Cube {
    param([string]$Position)
    $r = SendOk 'menu "Add/Mesh Object"'
    $name = ($r[0].Text -replace '^Created [^:]+:\s*', '').Trim()
    SendOk "select $name", "set_position $Position" | Out-Null
    return $name
}

function Get-EntityLine {
    param([string]$Name)
    return ((SendOk 'list_entities')[0].Payload | Where-Object { ($_ -split ' ')[0] -eq $Name })
}

Test 'a child keeps its place in the world: its transform is now relative to the parent' {
    $script:parent = New-Cube -Position '10 0 0'
    $script:child = New-Cube -Position '12 0 4'
    $r = SendOk "set_parent $script:child $script:parent"
    Assert-Match -Pattern 'is now a child of' -Actual $r[0].Text
    Assert-Match -Pattern "parent=$script:parent" -Actual (Get-EntityLine -Name $script:child)
    $p = Get-Position -Session $Session -Entity $script:child
    Assert-Near -Expected 2 -Actual $p.x -Tolerance 0.001 -Message 'x relative to the parent'
    Assert-Near -Expected 4 -Actual $p.z -Tolerance 0.001 -Message 'z relative to the parent'
}

Test 'letting it go puts the world position back' {
    SendOk "set_parent $script:child none" | Out-Null
    Assert-NotMatch -Pattern 'parent=' -Actual (Get-EntityLine -Name $script:child)
    $p = Get-Position -Session $Session -Entity $script:child
    Assert-Near -Expected 12 -Actual $p.x -Tolerance 0.001
    Assert-Near -Expected 4 -Actual $p.z -Tolerance 0.001
}

Test 'a turned parent turns the child with it, and the world position is still kept' {
    SendOk "select $script:parent", 'set_rotation 0 90 0' | Out-Null
    SendOk "set_parent $script:child $script:parent" | Out-Null
    $p = Get-Position -Session $Session -Entity $script:child
    # (2,0,4) from the parent, seen in a frame turned 90 degrees about Y: the same length, turned.
    Assert-Near -Expected ([Math]::Sqrt(20)) -Actual ([Math]::Sqrt($p.x * $p.x + $p.z * $p.z)) -Tolerance 0.001 -Message 'distance from the parent'
    Assert-Near -Expected 0 -Actual $p.y -Tolerance 0.001
    SendOk "set_parent $script:child none" | Out-Null
    $q = Get-Position -Session $Session -Entity $script:child
    Assert-Near -Expected 12 -Actual $q.x -Tolerance 0.001 -Message 'back in the world where it was'
    Assert-Near -Expected 4 -Actual $q.z -Tolerance 0.001
}

Test 'the hierarchy is one level deep, and nothing is its own child' {
    $other = New-Cube -Position '0 0 0'
    SendOk "set_parent $script:child $script:parent" | Out-Null
    Assert-Err -Result (Send "set_parent $other $script:child")[0] -Pattern 'one level deep' -Message 'a child cannot be a parent'
    Assert-Err -Result (Send "set_parent $script:parent $other")[0] -Pattern 'one level deep' -Message 'a parent cannot be a child'
    Assert-Err -Result (Send "set_parent $other $other")[0] -Pattern 'own child'
    Assert-Err -Result (Send "set_parent $other nope")[0] -Pattern 'unknown entity'
    Assert-Err -Result (Send "set_parent")[0] -Pattern 'usage:'
}

Test 'parenting and letting go are undoable, pose included' {
    SendOk "set_parent $script:child none" | Out-Null
    SendOk "set_parent $script:child $script:parent" | Out-Null
    SendOk 'undo' | Out-Null
    Assert-NotMatch -Pattern 'parent=' -Actual (Get-EntityLine -Name $script:child)
    Assert-Near -Expected 12 -Actual (Get-Position -Session $Session -Entity $script:child).x -Tolerance 0.001
    SendOk 'redo' | Out-Null
    Assert-Match -Pattern "parent=$script:parent" -Actual (Get-EntityLine -Name $script:child)
}

Test 'a parent and child survive a save and reload' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = $LevelPath
    SendOk "open_level $level" | Out-Null
    Assert-Match -Pattern "parent=$script:parent" -Actual (Get-EntityLine -Name $script:child)
}
