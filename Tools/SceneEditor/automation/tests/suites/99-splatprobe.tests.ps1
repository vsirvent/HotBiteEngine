# fixture: empty
# description: throwaway probe - measures the depth slab / opaque stop response.

$Parked = '0 -60 0'

function New-RenderPly {
    param([string]$Path, [int]$Count = 600)
    $props = @('x', 'y', 'z', 'rot_0', 'rot_1', 'rot_2', 'rot_3',
               'scale_0', 'scale_1', 'scale_2', 'opacity', 'f_dc_0', 'f_dc_1', 'f_dc_2')
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex $Count`n"
    foreach ($p in $props) { $header += "property float $p`n" }
    $header += "end_header`n"
    $body = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($body)
    $golden = 2.39996322972865332
    for ($i = 0; $i -lt $Count; $i++) {
        $y = 1.0 - 2.0 * ($i + 0.5) / $Count
        $r = [Math]::Sqrt([Math]::Max(0.0, 1.0 - $y * $y))
        $theta = $golden * $i
        $w.Write([float]([Math]::Cos($theta) * $r * 0.5))
        $w.Write([float]($y * 0.5))
        $w.Write([float]([Math]::Sin($theta) * $r * 0.5))
        $w.Write([float]1.0); $w.Write([float]0.0)
        $w.Write([float]0.0); $w.Write([float]0.0)
        $w.Write([float](-2.6)); $w.Write([float](-2.6)); $w.Write([float](-2.6))
        $w.Write([float]4.0)
        $w.Write([float]1.77); $w.Write([float]1.77); $w.Write([float]1.77)
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

function Move-Entity {
    param([string]$Entity, [string]$Position)
    SendOk "select $Entity", "set_position $Position", 'deselect' | Out-Null
}

function Remove-StandInMesh {
    param([string]$Entity)
    $before = SendOk "components $Entity"
    if ($before[0].Payload -contains 'Mesh') { SendOk "remove_component $Entity Mesh" | Out-Null }
}

function Initialize-Suite {
    if ($script:cloud) { return }
    $ply = Join-Path $Assets 'Objects\probecloud.ply'
    New-RenderPly -Path $ply
    SendOk "import_model ""$ply""" | Out-Null
    SendOk 'create_template_from_model probecloud splat_obj' | Out-Null
    SendOk 'place splat_obj' | Out-Null
    $names = Get-EntityNames -Session $Session
    $script:cloud = @($names | Where-Object { $_ -match '^splat_obj' })[0]
    Remove-StandInMesh -Entity $script:cloud
    foreach ($e in @('box_a', 'box_b', 'box_c')) { Move-Entity -Entity $e -Position $Parked }
    SendOk 'deselect', 'render debug_buffer off', 'render debug_gain 1' | Out-Null
}

function Set-Knobs {
    param([string]$Json)
    SendOk "set_component $($script:cloud) SplatCloud ""$Json""" | Out-Null
}

Test 'probe: colour response to depth_slab' {
    Initialize-Suite
    SendOk "select $($script:cloud)", 'set_position 0 0 0', 'set_scale 1 1 1', 'deselect' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -3', 'camera_target 0 0 0' | Out-Null
    SendOk 'render debug_buffer off' | Out-Null

    foreach ($op in @(1.0, 0.2)) {
        foreach ($sa in @(0.5, 0.05)) {
            $shots = @{}
            foreach ($slab in @(0.001, 0.02, 0.05, 0.2, 1.0)) {
                Set-Knobs "{'opacity_scale':$op,'surface_alpha':$sa,'depth_slab':$slab}"
                Step-EditorFrames -Session $Session -Count 6
                $p = Shot ("c-op$op-sa$sa-sl$slab" -replace '[.,]', '_')
                $shots[$slab] = $p
                $s = Get-ImageStats -Path $p -Left 0.44 -Right 0.56 -Top 0.42 -Bottom 0.58
                Write-Host ("    PROBE colour op=$op sa=$sa slab=$slab mean={0:N2} lit={1:N3}" -f $s.Mean, $s.LitShare)
            }
            $d = Get-ImageDifference -PathA $shots[0.02] -PathB $shots[1.0] -Left 0.44 -Right 0.56 -Top 0.42 -Bottom 0.58
            Write-Host ("    PROBE DIFF(0.02 vs 1.0) op=$op sa=$sa share={0:N3} mean={1:N2} max={2}" -f $d.DifferingShare, $d.MeanDelta, $d.MaxDelta)
            $d2 = Get-ImageDifference -PathA $shots[0.02] -PathB $shots[0.05] -Left 0.44 -Right 0.56 -Top 0.42 -Bottom 0.58
            Write-Host ("    PROBE DIFF(0.02 vs 0.05) op=$op sa=$sa share={0:N3} mean={1:N2} max={2}" -f $d2.DifferingShare, $d2.MeanDelta, $d2.MaxDelta)
        }
    }
}

Test 'probe: depth response to depth_slab' {
    Initialize-Suite
    SendOk "select $($script:cloud)", 'set_position 0 0 0', 'set_scale 8 8 8', 'deselect' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -25', 'camera_target 0 0 0' | Out-Null
    SendOk 'render debug_buffer depth', 'render debug_gain 1' | Out-Null

    foreach ($op in @(1.0, 0.2)) {
        foreach ($slab in @(0.001, 0.02, 0.05, 0.2, 1.0)) {
            Set-Knobs "{'opacity_scale':$op,'surface_alpha':0.5,'depth_slab':$slab}"
            Step-EditorFrames -Session $Session -Count 6
            $p = Shot ("d-op$op-sl$slab" -replace '[.,]', '_')
            $s = Get-ImageStats -Path $p -Left 0.47 -Right 0.53 -Top 0.44 -Bottom 0.55
            Write-Host ("    PROBE depth op=$op slab=$slab mean={0:N3} lit={1:N3}" -f $s.Mean, $s.LitShare)
        }
    }
    SendOk 'render debug_buffer off' | Out-Null
}
