# fixture: empty
# description: Sky component: draw_background, cloud_speed and the ambient light that follows the sun.

# SkySystem runs on a background thread, so a value it writes shows up a few frames
# after the command that caused it. Polls instead of guessing a frame count.
function Read-AmbientUp {
    param([string]$Entity, [scriptblock]$Until)
    $amb = $null
    for ($i = 0; $i -lt 40; $i++) {
        Step-EditorFrames -Session $Session -Count 2
        $amb = Get-Component -Session $Session -Entity $Entity -Component 'AmbientLight'
        if (& $Until $amb) { break }
        Start-Sleep -Milliseconds 50
    }
    return $amb
}

Test 'a new sky keeps the old behaviour by default' {
    SendOk 'menu "Add/Sky"' | Out-Null
    $script:SkyName = (Get-State -Session $Session).selected_entity_name
    $sky = Get-Component -Session $Session -Entity $SkyName -Component 'Sky'
    Assert-Equal -Expected $true -Actual $sky.draw_background
    Assert-Equal -Expected -1 -Actual ([int]$sky.cloud_speed)
    Assert-Equal -Expected $false -Actual $sky.ambient_cycle
}

Test 'draw_background, cloud_speed and the ambient pairs round-trip' {
    SendOk "set_component $SkyName Sky ""{'draw_background':false,'cloud_speed':2.5,'ambient_cycle':true,'ambient_night_up':{'x':0.1,'y':0.2,'z':0.3}}""" | Out-Null
    $sky = Get-Component -Session $Session -Entity $SkyName -Component 'Sky'
    Assert-Equal -Expected $false -Actual $sky.draw_background
    Assert-Equal -Expected 2.5 -Actual ([double]$sky.cloud_speed)
    Assert-Equal -Expected $true -Actual $sky.ambient_cycle
    Assert-Equal -Expected 0.2 -Actual ([math]::Round([double]$sky.ambient_night_up.y, 3))
}


# The Add/Sky preset has no Mesh or Material, so SkySystem never registers it - and a
# sky only runs through the system once it has both. This builds one the way a game
# does: a mesh entity that gains the sky, the sun and the ambient light. dir_light is
# never set by hand; the system wires it, and the renderer would fault without it.
Test 'a sky assembled from components runs through the system' {
    SendOk "select $SkyName", 'delete' | Out-Null
    SendOk 'menu "Add/Mesh Object"' | Out-Null
    $script:SkyName = (Get-State -Session $Session).selected_entity_name
    SendOk "add_component $SkyName DirectionalLight",
           "add_component $SkyName AmbientLight",
           "add_component $SkyName Sky" | Out-Null
    SendOk "set_component $SkyName Sky ""{'draw_background':false,'second_speed':0,'ambient_cycle':true,'ambient_day_up':{'x':0.8,'y':0.8,'z':0.8},'ambient_day_down':{'x':0.9,'y':0.9,'z':0.9},'ambient_night_up':{'x':0.02,'y':0.02,'z':0.02},'ambient_night_down':{'x':0.04,'y':0.04,'z':0.04},'second_of_day':0}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 10
}

Test 'the ambient light is dark at midnight and bright at noon' {
    $night = Read-AmbientUp -Entity $SkyName -Until { param($a) [math]::Abs($a.color_up.x - 0.02) -lt 0.005 }
    Assert-Equal -Expected 0.02 -Actual ([math]::Round([double]$night.color_up.x, 3)) -Message 'midnight ambient is the night colour'

    SendOk "set_component $SkyName Sky ""{'second_of_day':43200}""" | Out-Null
    $day = Read-AmbientUp -Entity $SkyName -Until { param($a) $a.color_up.x -gt 0.7 }
    Assert-Equal -Expected 0.8 -Actual ([math]::Round([double]$day.color_up.x, 3)) -Message 'noon ambient up is the day colour'
    Assert-Equal -Expected 0.9 -Actual ([math]::Round([double]$day.color_down.x, 3)) -Message 'noon ambient down is the day colour'
}

Test 'dusk sits between the night and day colours' {
    # 6:00 - the sun on the horizon, so halfway through the blend.
    SendOk "set_component $SkyName Sky ""{'second_of_day':21600}""" | Out-Null
    $mid = Read-AmbientUp -Entity $SkyName -Until { param($a) $a.color_up.x -gt 0.2 -and $a.color_up.x -lt 0.6 }
    Assert-True -Condition ($mid.color_up.x -gt 0.2 -and $mid.color_up.x -lt 0.6) -Message 'the horizon ambient is a blend, not either extreme'
}

Test 'with the cycle off the authored ambient is left alone' {
    SendOk "set_component $SkyName AmbientLight ""{'color_up':{'x':0.5,'y':0.5,'z':0.5}}""" | Out-Null
    SendOk "set_component $SkyName Sky ""{'ambient_cycle':false,'second_of_day':0}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 10
    Start-Sleep -Milliseconds 300
    $amb = Get-Component -Session $Session -Entity $SkyName -Component 'AmbientLight'
    Assert-Equal -Expected 0.5 -Actual ([math]::Round([double]$amb.color_up.x, 3))
}

Test 'the sun still follows the clock with the dome off' {
    SendOk "set_component $SkyName Sky ""{'second_of_day':43200}""" | Out-Null
    $dl = $null
    for ($i = 0; $i -lt 40; $i++) {
        Step-EditorFrames -Session $Session -Count 2
        $dl = Get-Component -Session $Session -Entity $SkyName -Component 'DirectionalLight'
        if ($dl.intensity -gt 0.9) { break }
        Start-Sleep -Milliseconds 50
    }
    Assert-True -Condition ($dl.intensity -gt 0.9) -Message 'the noon sun lights the scene with draw_background off'
}
