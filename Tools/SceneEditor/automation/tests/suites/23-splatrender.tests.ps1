# fixture: empty
# description: The Gaussian splat rasterizer - that a SplatCloud reaches the frame, moves with its entity, writes depth, and answers its per-entity knobs.
#
# The other half of 22-splats, which covers authoring and deliberately stops short
# of asserting that a cloud renders. This is the pass: RenderSystem::DrawSplats,
# SplatPreprocessCS (project and bin) and SplatRasterCS (rasterize and light).
#
# The subject is a cloud imported from a .ply and placed through a template, with
# the template's stand-in cube then taken off the instance (Remove-StandInMesh, and
# read its comment - the cube is mandatory on a template, so every placed cloud
# arrives wearing one). What is left is an entity with a SplatCloud and no Mesh,
# which is the case SplatCloudSystem exists for: Transform::world_matrix is written
# by StaticMeshSystem (needs Mesh and Bounds) and PhysicsSystem (needs a body), so
# without that system a cloud entity's matrix is never composed at all - and a zero
# matrix sends every splat to the origin with w = 0. Left on, the cube would both
# be measured instead of the cloud and hand the transform back to StaticMeshSystem,
# which is to say every test below would pass without the pass working.
#
# Two properties of the pass shape the assertions:
#
#  - A cloud is composited into the *same* targets MainRenderPS writes - scene
#    colour, the light map, the depth buffer - so it shows up in the ordinary frame
#    and in the `scene`/`depth` debug buffers, never in a channel of its own.
#  - The pass is not temporal: it accumulates nothing between frames. The rest of
#    the frame still does, so every capture is preceded by Step-EditorFrames.

# Far enough below the view to be gone, near enough to come back from.
$Parked = '0 -60 0'

# A denser, fatter cloud than 22-splats generates: that one exists to prove the
# header parser, this one has to cover pixels. A shell of splats about a unit
# across, opaque, bright.
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
        # Fibonacci sphere, same construction as the built-in stand-in: uniform
        # over the shell, with no pole clustering to make coverage uneven.
        $y = 1.0 - 2.0 * ($i + 0.5) / $Count
        $r = [Math]::Sqrt([Math]::Max(0.0, 1.0 - $y * $y))
        $theta = $golden * $i
        $w.Write([float]([Math]::Cos($theta) * $r * 0.5))
        $w.Write([float]($y * 0.5))
        $w.Write([float]([Math]::Sin($theta) * $r * 0.5))
        $w.Write([float]1.0); $w.Write([float]0.0)
        $w.Write([float]0.0); $w.Write([float]0.0)
        # exp(-2.6) ~ 0.074 world units across, so neighbours overlap and the shell
        # reads as a surface rather than as dots.
        $w.Write([float](-2.6)); $w.Write([float](-2.6)); $w.Write([float](-2.6))
        $w.Write([float]4.0)                                # opacity, pre-sigmoid: ~0.98
        $w.Write([float]1.77); $w.Write([float]1.77); $w.Write([float]1.77)
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

function Reset-SplatView {
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -3', 'camera_target 0 0 0' | Out-Null
}

# The share of the middle of the view that is lit. The fixture's cubes are parked
# and its level is otherwise empty, so with the camera above this is the cloud's
# coverage and nothing else.
function Get-CloudShare {
    param([string]$Name)
    Step-EditorFrames -Session $Session -Count 6
    return (Get-ImageStats -Path (Shot $Name)).LitShare
}

function Move-Entity {
    param([string]$Entity, [string]$Position)
    SendOk "select $Entity", "set_position $Position", 'deselect' | Out-Null
}

# Takes the stand-in cube off a placed cloud instance, and asserts it is gone.
#
# TemplatePanel::IsMandatory makes Mesh, Material and Bounds mandatory on a
# *template* - they are what World::SpawnInstance clones - so a template built from
# a .ply carries a default cube alongside its cloud, and every instance is placed
# wearing one. That cube is not a bug, but it is fatal to this suite twice over: it
# sits exactly where the cloud is and would be measured instead of it, and an entity
# with a Mesh and a Bounds is one StaticMeshSystem owns, so the transform test would
# pass whether SplatCloudSystem exists or not. Both are silent, so this asserts
# rather than tries.
function Remove-StandInMesh {
    param([string]$Entity)
    $before = SendOk "components $Entity"
    if ($before[0].Payload -contains 'Mesh') {
        SendOk "remove_component $Entity Mesh" | Out-Null
    }
    $after = SendOk "components $Entity"
    Assert-NotContains -Collection $after[0].Payload -Value 'Mesh' `
        -Message "$Entity has to be a cloud and nothing else"
}

# Imports the cloud, makes a template of it and places one - the same three steps
# the Asset Browser takes - then parks everything the fixture came with so the
# cloud is the only thing in shot. Runs once; every test re-poses the instance it
# leaves behind rather than placing another.
function Initialize-Suite {
    if ($script:cloud) { return }
    $ply = Join-Path $Assets 'Objects\rendercloud.ply'
    New-RenderPly -Path $ply
    SendOk "import_model ""$ply""" | Out-Null
    SendOk 'create_template_from_model rendercloud splat_obj' | Out-Null
    SendOk 'place splat_obj' | Out-Null
    # Assigned before it is filtered, and @() around the filter. Both matter, and
    # both cost a run here: Get-EntityNames ends in `return , $names`, so piping its
    # result straight into Where-Object hands over the whole array as ONE object
    # (which -match happily matches, so the filter passes everything through); and
    # [0] on a lone string is its first *character* rather than the string.
    $names = Get-EntityNames -Session $Session
    $script:cloud = @($names | Where-Object { $_ -match '^splat_obj' })[0]
    Assert-True -Condition ($null -ne $script:cloud) -Message 'the cloud instance was placed'
    Remove-StandInMesh -Entity $script:cloud

    foreach ($e in @('box_a', 'box_b', 'box_c')) { Move-Entity -Entity $e -Position $Parked }
    SendOk 'deselect', 'render debug_buffer off', 'render debug_gain 1' | Out-Null
}

# Back to the state every test starts from: cloud at the origin, unit scale,
# visible, default knobs, view framed on it.
function Reset-Cloud {
    Initialize-Suite
    SendOk "select $($script:cloud)", 'set_position 0 0 0', 'set_scale 1 1 1' | Out-Null
    SendOk "set_component $($script:cloud) SplatCloud ""{'opacity_scale':1.0,'albedo_scale':1.0,'invert_normals':false,'surface_alpha':0.5}""" | Out-Null
    SendOk "set_component $($script:cloud) Base ""{'visible':true}""" | Out-Null
    SendOk 'deselect', 'render debug_buffer off', 'render debug_gain 1' | Out-Null
    Reset-SplatView
}

Test 'a placed splat cloud reaches the frame' {
    # The whole point of the pass. If this fails while 22-splats still passes, the
    # component is being authored correctly and simply never drawn - which is the
    # state this suite exists to catch.
    Reset-Cloud
    $with = Get-CloudShare 'cloud-on'
    Move-Entity -Entity $script:cloud -Position $Parked
    $without = Get-CloudShare 'cloud-away'
    Assert-True -Condition ($with -gt 0.2) `
        -Message "the cloud should cover the middle of the view ($([Math]::Round($with * 100, 1))% lit)"
    Assert-True -Condition ($without -lt $with - 0.15) `
        -Message "and the same view without it should be darker (with $([Math]::Round($with * 100, 1))%, without $([Math]::Round($without * 100, 1))%)"
}

Test 'the cloud follows its transform' {
    # That the world matrix reaches the pass, and with it that SplatCloudSystem is
    # registered and ticking. This entity has no Mesh, so nothing else composes its
    # matrix; the two failure modes - cloud pinned to the origin, cloud never drawn -
    # both land here. It is a round trip because a one-way move can be passed by a
    # pass that simply lost the entity.
    Reset-Cloud
    $centred = Get-CloudShare 'transform-centre'
    Move-Entity -Entity $script:cloud -Position $Parked
    $moved = Get-CloudShare 'transform-moved'
    Assert-True -Condition ($moved -lt $centred - 0.15) `
        -Message "moving the entity should take the cloud with it (centred $([Math]::Round($centred * 100, 1))%, moved $([Math]::Round($moved * 100, 1))%)"

    Move-Entity -Entity $script:cloud -Position '0 0 0'
    $back = Get-CloudShare 'transform-back'
    Assert-True -Condition ($back -gt $moved + 0.15) `
        -Message "and moving it back should bring the cloud back ($([Math]::Round($back * 100, 1))% lit)"
}

Test 'scale reaches the pass through the same matrix' {
    # The covariance is transformed by the world matrix's upper 3x3
    # (Sigma' = M Sigma M^T), not just the centres, so a scaled cloud is bigger in
    # both senses. A pass that transformed positions but not covariances would still
    # move the splats apart and would still grow the coverage - but a pass that
    # dropped scale from the matrix entirely fails here.
    Reset-Cloud
    $unit = Get-CloudShare 'scale-1'
    SendOk "select $($script:cloud)", 'set_scale 2 2 2', 'deselect' | Out-Null
    $big = Get-CloudShare 'scale-2'
    Assert-True -Condition ($big -gt $unit) `
        -Message "scaling the entity up should cover more of the view (1x $([Math]::Round($unit * 100, 1))%, 2x $([Math]::Round($big * 100, 1))%)"
}

Test 'Base.visible takes the cloud out of the frame' {
    # 22-splats asserts that `visible` is the flag that decides this; here it has to
    # actually decide it, in the pass rather than in the component. Both directions,
    # so a pass that dropped the cloud on the way out cannot pass.
    Reset-Cloud
    $on = Get-CloudShare 'visible-on'
    SendOk "set_component $($script:cloud) Base ""{'visible':false}""" | Out-Null
    $off = Get-CloudShare 'visible-off'
    Assert-True -Condition ($off -lt $on - 0.15) `
        -Message "hiding the entity should remove the cloud (visible $([Math]::Round($on * 100, 1))%, hidden $([Math]::Round($off * 100, 1))%)"

    SendOk "set_component $($script:cloud) Base ""{'visible':true}""" | Out-Null
    $again = Get-CloudShare 'visible-again'
    Assert-True -Condition ($again -gt $off + 0.15) `
        -Message "showing it again should bring the cloud back ($([Math]::Round($again * 100, 1))% lit)"
}

Test 'the cloud writes the depth buffer, not only colour' {
    # DrawSplats writes three targets, and depth is the one with consequences beyond
    # the picture: the ray tracers, the autofocus and every effect reading world
    # distance work off it. A pass that wrote colour alone would look right in a
    # screenshot and be wrong everywhere else, so depth is asserted separately.
    #
    # Measured from 25 units back rather than from the 3 the other tests use. The
    # depth view maps world distance as 1-exp(-d/100), so a cloud at 3 units lands on
    # 7/255 - a correct write that is indistinguishable from black at any sane
    # threshold, and it reads as "the pass writes no depth". At 25 units it is ~56,
    # and the cloud is scaled to keep the same share of the view.
    Reset-Cloud
    SendOk "select $($script:cloud)", 'set_scale 8 8 8', 'deselect' | Out-Null
    SendOk 'camera_pos 0 0 -25' | Out-Null
    SendOk 'render debug_buffer depth' | Out-Null
    Move-Entity -Entity $script:cloud -Position $Parked
    Step-EditorFrames -Session $Session -Count 6
    $without = Shot 'depth-without'

    Move-Entity -Entity $script:cloud -Position '0 0 0'
    Step-EditorFrames -Session $Session -Count 6
    $with = Shot 'depth-with'

    # Compared over the middle of the view, not the default near-whole-window box:
    # the cloud is a disc covering about 1.5% of the latter, so a real write reads as
    # a rounding error there. Inside its own footprint it is all-or-nothing.
    $diff = Get-ImageDifference -PathA $without -PathB $with `
        -Left 0.47 -Right 0.53 -Top 0.44 -Bottom 0.55
    SendOk 'render debug_buffer off' | Out-Null
    Assert-True -Condition ($diff.DifferingShare -gt 0.5) `
        -Message "the cloud should appear in the depth buffer where it is drawn, only $([Math]::Round($diff.DifferingShare * 100, 1))% of those pixels differ"
}

Test 'opacity_scale fades the cloud out' {
    # The per-entity knob with the most direct effect on the pass: it multiplies
    # every splat's alpha in SplatPreprocessCS, and below SPLAT_MIN_ALPHA a splat is
    # dropped before it is ever binned - so at zero the cloud is gone. Also the
    # cheapest check that the preprocess cbuffer reaches the shader at all.
    Reset-Cloud
    $opaque = Get-CloudShare 'opacity-1'
    SendOk "set_component $($script:cloud) SplatCloud ""{'opacity_scale':0.0}""" | Out-Null
    $clear = Get-CloudShare 'opacity-0'
    Assert-True -Condition ($clear -lt $opaque - 0.15) `
        -Message "opacity_scale 0 should remove the cloud (1.0 -> $([Math]::Round($opaque * 100, 1))%, 0.0 -> $([Math]::Round($clear * 100, 1))%)"
}

Test 'albedo_scale changes the colour without changing the coverage' {
    # albedo_scale multiplies colour only; it never reaches the alpha that decides
    # which pixels the cloud covers. Asserting both halves is what separates "the
    # tint works" from "the tint is being applied to the wrong thing" - a scale that
    # leaked into opacity would pass a brightness check on its own.
    Reset-Cloud
    Step-EditorFrames -Session $Session -Count 6
    $bright = Get-ImageStats -Path (Shot 'albedo-1')

    SendOk "set_component $($script:cloud) SplatCloud ""{'albedo_scale':0.15}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $dim = Get-ImageStats -Path (Shot 'albedo-dim')

    Assert-True -Condition ($dim.Mean -lt $bright.Mean - 3) `
        -Message "a lower albedo_scale should darken the cloud (mean $([Math]::Round($bright.Mean, 1)) -> $([Math]::Round($dim.Mean, 1)))"
    Assert-True -Condition ([Math]::Abs($dim.LitShare - $bright.LitShare) -lt 0.25) `
        -Message "albedo_scale should not change what the cloud covers ($([Math]::Round($bright.LitShare * 100, 1))% -> $([Math]::Round($dim.LitShare * 100, 1))%)"
}

Test 'a second cloud draws too' {
    # The pass loops over entities, sizing its scratch buffers for the largest cloud
    # and re-binning per cloud. A loop that left the previous cloud's tile counts in
    # place, or sized its buffers from the first entity only, still draws one cloud
    # correctly - so one cloud proves less than it looks like it does.
    Reset-Cloud
    Move-Entity -Entity $script:cloud -Position '-0.6 0 0'
    $one = Get-CloudShare 'two-one'

    SendOk 'place splat_obj' | Out-Null
    $names = Get-EntityNames -Session $Session
    $second = @($names | Where-Object { $_ -match '^splat_obj' -and $_ -ne $script:cloud })[0]
    Assert-True -Condition ($null -ne $second) -Message 'a second instance was placed'
    Remove-StandInMesh -Entity $second
    Move-Entity -Entity $second -Position '0.6 0 0'
    $two = Get-CloudShare 'two-both'

    Assert-True -Condition ($two -gt $one) `
        -Message "a second cloud should add to the frame (one $([Math]::Round($one * 100, 1))%, two $([Math]::Round($two * 100, 1))%)"
    Move-Entity -Entity $second -Position $Parked
}

Test 'opaque geometry in front of a cloud hides it' {
    # SplatPreprocessCS rejects a splat behind the opaque surface recorded by the
    # depth pre-pass. Put a cube between the camera and the cloud and the cloud must
    # lose; move the same cube behind it and the cloud must come back. Without the
    # depth test the cloud wins in both and the two frames are the same.
    Reset-Cloud
    SendOk 'select box_c', 'set_scale 2 2 2' | Out-Null
    SendOk 'set_component box_c Base "{''visible'':true}"', 'deselect' | Out-Null

    Move-Entity -Entity 'box_c' -Position '0 0 2'
    Step-EditorFrames -Session $Session -Count 6
    $behind = Shot 'occlusion-cube-behind'

    Move-Entity -Entity 'box_c' -Position '0 0 -1.5'
    Step-EditorFrames -Session $Session -Count 6
    $infront = Shot 'occlusion-cube-in-front'

    $diff = Get-ImageDifference -PathA $behind -PathB $infront
    Move-Entity -Entity 'box_c' -Position $Parked
    Assert-True -Condition ($diff.DifferingShare -gt 0.05) `
        -Message "a cube in front of the cloud should change the frame, only $([Math]::Round($diff.DifferingShare * 100, 1))% of pixels differ"
}
