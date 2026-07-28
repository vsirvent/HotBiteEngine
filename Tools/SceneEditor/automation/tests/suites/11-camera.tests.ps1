# fixture: empty
# description: The editor camera - pose, orbit/pan/dolly/fly, and the one-frame lag of the rendered position.

Test 'camera reports the orbit pose the fixture set up' {
    $cam = Get-Camera -Session $Session
    Assert-Vector3Near -Expected @{ x = 12.0; y = 9.0; z = -14.0 } `
        -Actual @{ x = $cam.position[0]; y = $cam.position[1]; z = $cam.position[2] }
    Assert-Near -Expected 20.518 -Actual $cam.distance -Tolerance 0.01 -Message 'distance to the focus point'
}

Test 'camera_pos moves the orbit position and leaves the target' {
    SendOk 'camera_target 0 0 0' | Out-Null
    SendOk 'camera_pos 10 0 0' | Out-Null
    $cam = Get-Camera -Session $Session
    Assert-Vector3Near -Expected @{ x = 10.0; y = 0.0; z = 0.0 } `
        -Actual @{ x = $cam.position[0]; y = $cam.position[1]; z = $cam.position[2] }
    Assert-Near -Expected 10.0 -Actual $cam.distance -Tolerance 0.001
}

Test 'camera_target moves the focus point' {
    SendOk 'camera_pos 10 0 0', 'camera_target 4 0 0' | Out-Null
    $cam = Get-Camera -Session $Session
    Assert-Near -Expected 4.0 -Actual $cam.target[0] -Tolerance 0.001
    Assert-Near -Expected 6.0 -Actual $cam.distance -Tolerance 0.001
}

Test 'camera_rot takes degrees and reports them back' {
    SendOk 'camera_rot 15 45 0' | Out-Null
    $cam = Get-Camera -Session $Session
    Assert-Near -Expected 15.0 -Actual $cam.rotation_deg[0] -Tolerance 0.01 -Message 'pitch'
    Assert-Near -Expected 45.0 -Actual $cam.rotation_deg[1] -Tolerance 0.01 -Message 'yaw'
    SendOk 'camera_rot 0 0 0' | Out-Null
}

Test 'the camera commands validate their arguments' {
    foreach ($cmd in @('camera_pos 1 2', 'camera_target x y z', 'camera_rot 1', 'camera_orbit 5',
                       'camera_zoom', 'camera_fly 1 2')) {
        Assert-Err -Result (Send $cmd)[0] -Pattern 'usage:' -Message $cmd
    }
}

Test 'camera_orbit turns the rig around the focus point' {
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -20', 'camera_target 0 0 0' | Out-Null
    $before = Get-Camera -Session $Session
    SendOk 'camera_orbit 120 0' | Out-Null
    $after = Get-Camera -Session $Session
    # An orbit is a rotation of the rig, so it shows up in rotation_deg (which the
    # renderer then swings the eye around by) rather than in the orbit position.
    Assert-True -Condition ([Math]::Abs($after.rotation_deg[1] - $before.rotation_deg[1]) -gt 1.0) `
        -Message 'a horizontal drag yaws the camera'
    Assert-Vector3Near -Expected @{ x = 0.0; y = 0.0; z = 0.0 } `
        -Actual @{ x = $after.target[0]; y = $after.target[1]; z = $after.target[2] } `
        -Tolerance 0.001 -Message 'the focus point is the pivot and does not move'
    Assert-Near -Expected $before.distance -Actual $after.distance -Tolerance 0.01 -Message 'orbit preserves the radius'
    SendOk 'camera_rot 0 0 0' | Out-Null
}

Test 'camera_pan moves camera and focus point together' {
    SendOk 'camera_pos 0 0 -20', 'camera_target 0 0 0' | Out-Null
    $before = Get-Camera -Session $Session
    SendOk 'camera_pan 100 0' | Out-Null
    $after = Get-Camera -Session $Session
    Assert-Near -Expected $before.distance -Actual $after.distance -Tolerance 0.01 -Message 'the rig translates'
    $movedTarget = [Math]::Abs($after.target[0] - $before.target[0]) + [Math]::Abs($after.target[1] - $before.target[1])
    Assert-True -Condition ($movedTarget -gt 0.01) -Message 'the focus point came along'
}

Test 'camera_zoom dollies toward the focus point and stops before it' {
    SendOk 'camera_pos 0 0 -20', 'camera_target 0 0 0' | Out-Null
    $before = Get-Camera -Session $Session
    SendOk 'camera_zoom 3' | Out-Null
    $after = Get-Camera -Session $Session
    Assert-True -Condition ($after.distance -lt $before.distance) -Message 'positive steps come closer'

    SendOk 'camera_zoom 500' | Out-Null
    $clamped = Get-Camera -Session $Session
    Assert-True -Condition ($clamped.distance -gt 0.0) -Message 'the dolly is clamped before the focus point'
}

Test 'camera_fly translates the whole rig by the distance asked for' {
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -20', 'camera_target 0 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $before = Get-Camera -Session $Session
    SendOk 'camera_fly 5 0 0' | Out-Null
    $after = Get-Camera -Session $Session

    # Camera and focus point move by the same vector - the rig translates, the
    # view direction and orbit radius are preserved - and the step is 5 world
    # units along whichever way the camera is facing.
    # Parenthesised on purpose: PowerShell binds the comma tighter than the minus,
    # so `$a[0] - $b[0], $a[1] - $b[1]` would subtract an array.
    $dcam = @(($after.position[0] - $before.position[0]),
              ($after.position[1] - $before.position[1]),
              ($after.position[2] - $before.position[2]))
    $dtarget = @(($after.target[0] - $before.target[0]),
                 ($after.target[1] - $before.target[1]),
                 ($after.target[2] - $before.target[2]))
    for ($i = 0; $i -lt 3; $i++) {
        Assert-Near -Expected $dcam[$i] -Actual $dtarget[$i] -Tolerance 0.001 -Message "rig component $i"
    }
    $length = [Math]::Sqrt($dcam[0] * $dcam[0] + $dcam[1] * $dcam[1] + $dcam[2] * $dcam[2])
    Assert-Near -Expected 5.0 -Actual $length -Tolerance 0.01 -Message 'distance flown'
    Assert-Near -Expected $before.distance -Actual $after.distance -Tolerance 0.01 -Message 'orbit radius preserved'
}

Test 'the rendered world position lags a move made in the same batch' {
    # CameraSystem consumes the change on the next world tick, which is why the
    # README says to query world_position in a follow-up batch.
    SendOk 'camera_rot 0 0 0', 'camera_target 0 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $r = SendOk 'camera_pos 33 0 -20', 'camera'
    $sameBatch = $r[1].Text | ConvertFrom-Json
    Assert-Near -Expected 33.0 -Actual $sameBatch.position[0] -Tolerance 0.001 -Message 'the orbit position is immediate'
    Assert-True -Condition ([Math]::Abs($sameBatch.world_position[0] - 33.0) -gt 1.0) `
        -Message 'the rendered position has not caught up yet'

    Step-EditorFrames -Session $Session -Count 3
    $settled = Get-Camera -Session $Session
    Assert-Near -Expected 33.0 -Actual $settled.world_position[0] -Tolerance 0.5 `
        -Message 'and after a few frames it has'
}

Test 'camera moves do not touch the scene' {
    $before = Get-Position -Session $Session -Entity 'box_a'
    SendOk 'camera_orbit 30 20', 'camera_pan 10 10', 'camera_zoom -2', 'camera_fly 1 1 1' | Out-Null
    Assert-Vector3Near -Expected $before -Actual (Get-Position -Session $Session -Entity 'box_a')
    Assert-Err -Result (Send 'undo')[0] -Message 'and nothing was recorded in the history'
}
