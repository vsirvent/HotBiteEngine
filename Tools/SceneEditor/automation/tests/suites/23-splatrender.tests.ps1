# fixture: empty
# description: The Gaussian splat rasterizer - that a SplatCloud reaches the frame, moves with its entity, writes depth, and answers its per-entity knobs.
#
# The other half of 22-splats, which covers authoring and deliberately stops short
# of asserting that a cloud renders. This is the pass: RenderSystem::DrawSplats,
# SplatPreprocessCS (project and bin) and SplatRasterCS (rasterize and light).
#
# The subject is a cloud imported from a .ply and placed through a template.
# Remove-StandInMesh is a no-op today (a splat template built from a model with no
# mesh nodes no longer gets a stand-in cube at all - see 22-splats.tests.ps1's
# "Create Template turns an imported cloud into a placeable object"), kept as a
# defensive call in case a future template genuinely does carry one. Either way,
# what these tests exercise is an entity with a SplatCloud and no Mesh, which is
# the case SplatCloudSystem exists for: Transform::world_matrix is written by
# StaticMeshSystem (needs Mesh and Bounds) and PhysicsSystem (needs a body), so
# without that system a cloud entity's matrix is never composed at all - and a zero
# matrix sends every splat to the origin with w = 0. A stray cube would both be
# measured instead of the cloud and hand the transform back to StaticMeshSystem,
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

# A cloud built to expose an ORDERING bug, which the one above cannot. Two properties
# do that and both are required:
#
#  - dense, so many splats land on one pixel AND share a depth quantization step. The
#    binning sorts to one step, so a step is where the residual disorder lives - the
#    entries in it are adjacent but in whatever order the atomics produced.
#  - a DIFFERENT COLOUR PER SPLAT. This is the half that is easy to miss and it cost a
#    round here: alpha compositing two co-located splats in either order gives the same
#    answer when their colours agree, so the 600-splat shell above - every splat the
#    same bright grey - renders identically however it is ordered. A guard written
#    against it passes with a deliberately broken composite.
function New-VariedPly {
    param([string]$Path, [int]$Count = 40000, [double]$LogScale = -3.4)
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
        $w.Write([float]1.0); $w.Write([float]0.0); $w.Write([float]0.0); $w.Write([float]0.0)
        $w.Write([float]$LogScale); $w.Write([float]$LogScale); $w.Write([float]$LogScale)
        $w.Write([float]4.0)
        # Hashed off the index, so neighbours differ sharply rather than varying
        # smoothly - a smooth gradient would make adjacent splats nearly the same
        # colour and blunt the very thing this is for.
        $h = ($i * 2654435761) -band 0xFFFFFF
        $w.Write([float](0.6 + 2.2 * (($h -band 0xFF) / 255.0)))
        $w.Write([float](0.6 + 2.2 * ((($h -shr 8) -band 0xFF) / 255.0)))
        $w.Write([float](0.6 + 2.2 * ((($h -shr 16) -band 0xFF) / 255.0)))
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

# A .ply that is NOT a trained 3DGS capture: positions and a vertex colour, and
# nothing else - no scale_*, no rot_*, no opacity, no f_dc_*. This is what a 3D
# scanner or a photogrammetry tool exports, and what SplatCloudData::Load has to fit
# normals for, every splat in it being an isotropic sphere with no minor axis to read
# one off.
#
# A sphere shell again, and deliberately so: every normal on it is different and each
# one is known in advance from where the point sits, which is what makes the fit
# checkable at all. Denser than the Gaussian fixture because the fit is over the
# NEIGHBOURS - 24 of them - so a handful of points scattered over a shell would be
# fitting a plane through most of the model.
function New-PointCloudPly {
    param([string]$Path, [int]$Count = 4000, [double]$Radius = 0.5)
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex $Count`n" +
              "property float x`nproperty float y`nproperty float z`n" +
              "property uchar red`nproperty uchar green`nproperty uchar blue`n" +
              "end_header`n"

    $body = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($body)
    $golden = 2.39996322972865332
    for ($i = 0; $i -lt $Count; $i++) {
        $y = 1.0 - 2.0 * ($i + 0.5) / $Count
        $r = [Math]::Sqrt([Math]::Max(0.0, 1.0 - $y * $y))
        $theta = $golden * $i
        $w.Write([float]([Math]::Cos($theta) * $r * $Radius))
        $w.Write([float]($y * $Radius))
        $w.Write([float]([Math]::Sin($theta) * $r * $Radius))
        # One flat bright grey. The colour is not what is being measured here and a
        # varying one would show up in the lit frame as texture that could be mistaken
        # for shading.
        $w.Write([byte]220); $w.Write([byte]220); $w.Write([byte]220)
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

function Reset-SplatView {
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -3', 'camera_target 0 0 0' | Out-Null
}

# What the binning did, once it has settled.
#
# splat_info comes from a GPU readback several frames behind the frame that produced
# it (Core::RWByteBuffer::Readback maps a slot from a few frames ago rather than
# stalling), so reading it straight after a change reports the state from before the
# change. Poll until two consecutive readings agree rather than sleeping a guessed
# number of frames.
function Get-SplatInfo {
    param($Session, [int]$Tries = 20)
    $prev = $null
    for ($i = 0; $i -lt $Tries; $i++) {
        Step-EditorFrames -Session $Session -Count 3
        $line = (SendOk 'splat_info')[0].Text
        $cur = @{}
        foreach ($m in [regex]::Matches($line, '(\w+)=(\d+)')) {
            $cur[$m.Groups[1].Value] = [int64]$m.Groups[2].Value
        }
        if ($null -ne $prev -and $prev.total_binned -eq $cur.total_binned -and
            $prev.pixels_written -eq $cur.pixels_written) {
            return [pscustomobject]$cur
        }
        $prev = $cur
    }
    return [pscustomobject]$prev
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

# Takes the stand-in cube off a placed cloud instance, if it has one, and asserts
# it is gone either way.
#
# A template used to force Mesh/Material/Bounds onto every template
# (TemplateOps::IsMandatory), so one built from a .ply carried a default cube
# alongside its cloud and every instance was placed wearing one - not a bug, but
# fatal to this suite twice over: the cube sits exactly where the cloud is and
# would be measured instead of it, and an entity with a Mesh and a Bounds is one
# StaticMeshSystem owns, so the transform test would pass whether SplatCloudSystem
# exists or not. Templates no longer force those components on (see
# 22-splats.tests.ps1), so a splat template built from a model with no mesh nodes
# has none to begin with - this is now a defensive no-op, kept in case that ever
# changes, both being silent enough that this asserts rather than tries.
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
    SendOk "set_component $($script:cloud) SplatCloud ""{'opacity_scale':1.0,'albedo_scale':1.0,'point_size_scale':1.0,'invert_normals':false,'surface_alpha':0.5}""" | Out-Null
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

Test 'max_density thins the cloud without removing it' {
    # The screen-coverage LOD, which is what stops the pass costing the same for a
    # cloud two units away and a hundred: it keeps a stable random subset sized to
    # hold max_density splats over the area the cloud projects to. splat_info reports
    # what reached the tiles, which is the only place the subset is visible - a
    # thinned cloud still looks like the cloud.
    #
    # Measured with the cloud pushed well back, and that is not incidental. The knob
    # binds only where the cloud is *over* its density, and this fixture's 600 splats
    # are nowhere near dense over the area they cover from the default view - at any
    # setting the keep probability clamps to 1 and nothing is dropped, which is the
    # correct answer and an untestable one. Far away the same 600 splats fall on a
    # couple of hundred pixels, and the knob starts to bite. 60 units is not enough -
    # the projected disc is still wider than 600 px there and every splat survives at
    # any setting.
    Reset-Cloud
    Move-Entity -Entity $script:cloud -Position '0 0 200'
    $full = Get-SplatInfo -Session $Session
    SendOk "set_component $($script:cloud) SplatCloud ""{'max_density':1.0}""" | Out-Null
    $thin = Get-SplatInfo -Session $Session

    Assert-True -Condition ($thin.total_binned -lt $full.total_binned) `
        -Message "a lower density should bin fewer splats (full $($full.total_binned), thinned $($thin.total_binned))"
    # Still drawn: the subset is smaller, not empty. That is the failure the floor in
    # FromJson exists to prevent, checked here in the pass rather than in the edit.
    Assert-True -Condition ($thin.pixels_written -gt 0) `
        -Message "and the cloud should still be rasterized ($($thin.pixels_written) pixels)"

    # Raising it back restores exactly the same set, which is the property that keeps
    # a cloud from boiling as the camera moves: the subset is a threshold on a stable
    # per-splat hash, so changing the density adds and removes splats rather than
    # reshuffling which ones are drawn.
    SendOk "set_component $($script:cloud) SplatCloud ""{'max_density':16.0}""" | Out-Null
    $back = Get-SplatInfo -Session $Session
    Assert-Equal -Expected $full.total_binned -Actual $back.total_binned `
        -Message 'and raising it back gives the same set'
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

Test 'the cloud renders opaque, not as alpha rings' {
    # That the surface the rasterizer accumulates is thick enough to make a covered
    # pixel OPAQUE, which no other test here looks at: every assertion above is about
    # coverage (does the cloud reach the frame, follow its transform, write depth) and
    # a cloud at half alpha satisfies all of them.
    #
    # The failure this guards is specific and was reached by deleting
    # splat_depth_slab - the tail SplatRasterCS gathers past the band where coverage
    # crosses surface_alpha. It looks removable: the crossing band already holds the
    # surface. What it actually does is carry acc_w past 1, where saturate() flattens
    # it. Without it a pixel's alpha is the sum over bands 0..k exactly, which is a
    # step function of which band the crossing landed in - so the object comes out in
    # concentric alpha rings at about half opacity. The G-buffer is untouched by that
    # (the depth view stays bit-identical, and the set of pixels passing the
    # `alpha > surface_alpha` gate does not change by one pixel, since the crossing
    # guarantees acc_w >= surface_alpha either way), so every other test in this file
    # passes while the cloud renders visibly wrong.
    #
    # Read off `ray_opacity`, whose red channel IS the alpha the rasterizer wrote
    # (DebugRayScalarColor maps c to (c, 1-|2c-1|, 1-c)) - so an opaque cloud is pure
    # red and green is the signature of alpha near 0.5. Green rather than red is the
    # measure because red saturates: the ringed cloud is still red at its very centre,
    # and only the mid-radius band structure separates the two - MeanR moves 251 -> 236
    # over the same pair, which is no margin at all. Measured on this fixture, one
    # build each way: MeanG 13.6 with the tail, 61.1 without, so the threshold sits
    # about halfway in the log sense rather than next to either.
    Reset-Cloud
    SendOk 'render debug_buffer ray_opacity', 'render debug_gain 1' | Out-Null
    Step-EditorFrames -Session $Session -Count 8
    $s = Get-ImageStats -Path (Shot 'opacity-rings') -Left 0.40 -Top 0.35 -Right 0.60 -Bottom 0.65
    SendOk 'render debug_buffer off' | Out-Null

    # The region has to be ON the cloud, or "not green" is a statement about the
    # background - the trap the suite README calls out.
    Assert-True -Condition ($s.MeanR -gt 150) `
        -Message "the sample region should be on the cloud (MeanR $([Math]::Round($s.MeanR, 1)))"
    Assert-True -Condition ($s.MeanG -lt 35) `
        -Message ("a covered pixel should be opaque, not half-alpha: green means alpha ~0.5 " +
                  "(MeanG $([Math]::Round($s.MeanG, 1)), MeanR $([Math]::Round($s.MeanR, 1)))")
}

Test 'the walk stops at the opaque surface instead of reading every entry' {
    # SplatRasterCS composites front to back and stops once transmittance is under
    # SPLAT_MIN_T, since nothing behind an opaque surface can move an accumulator by
    # more than that. This is the only test of it, and it needs a counter because the
    # feature is invisible by construction: a walk that stops early and one that reads
    # the whole slice render the SAME image - that is the point of it - so no
    # screenshot can tell them apart.
    #
    # `entries_walked` is the (pixel, entry) pairs examined, summed over every thread.
    # The reference is total_binned * 256: one thread per pixel of a 16x16 tile, each
    # reading every entry of its tile's slice. With the early-out compiled out
    # (SPLAT_EARLY_OUT 0) the counter equals that exactly, which is what makes it a
    # sound denominator rather than an estimate.
    #
    # The bound is loose on purpose. Threads whose pixel the cloud misses never build
    # any transmittance and so always walk their tile in full, so the ratio depends on
    # how much of each covered tile the cloud actually fills - a silhouette tile drags
    # it up. Measured on this fixture it sits far below the bound; the test is asking
    # "does the early-out fire at all", not pinning a performance number.
    Reset-Cloud
    $info = Get-SplatInfo -Session $Session
    $full = [double]$info.total_binned * 256.0
    Assert-True -Condition ($info.total_binned -gt 0) `
        -Message 'the cloud has to be binned for this to mean anything'
    Assert-True -Condition ($info.entries_walked -gt 0) `
        -Message 'the rasterizer has to have walked something'
    $ratio = $info.entries_walked / $full
    Assert-True -Condition ($ratio -lt 0.75) `
        -Message ("the walk should stop at the opaque surface, not read every entry: " +
                  "walked $($info.entries_walked) of $([int64]$full) ($([Math]::Round($ratio * 100, 1))%)")
}

Test 'the composite is stable frame to frame' {
    # Guards the one thing that made transmittance compositing hard here. The binning
    # sorts to a quantization step, so entries sharing a step are adjacent but in
    # whatever order the atomics produced, and that order changes every frame. Alpha
    # compositing is order-dependent EVEN BETWEEN CO-LOCATED SPLATS - c1*a1 +
    # c2*a2*(1-a1) is not the same as swapping them - so a naive front-to-back walk
    # boils. SplatRasterCS gathers each run of equal-depth entries into one layer and
    # composites it with a mean and a product, both symmetric, which removes it.
    #
    # Nothing in this pass is temporal, so on a frozen scene consecutive frames should
    # be identical bar the engine's own GI/denoiser floor.
    #
    # It needs its OWN cloud - dense and per-splat coloured, see New-VariedPly. Written
    # against the suite's usual 600-splat shell this passed with a deliberately broken
    # composite, because that shell is one flat colour and co-located splats of one
    # colour composite the same in any order. The subject has to be able to show the
    # bug before the assertion means anything.
    Reset-Cloud
    if (-not $script:varied) {
        $ply = Join-Path $Assets 'Objects\variedcloud.ply'
        New-VariedPly -Path $ply
        SendOk "import_model ""$ply""" | Out-Null
        SendOk 'create_template_from_model variedcloud varied_obj' | Out-Null
        SendOk 'place varied_obj' | Out-Null
        $names = Get-EntityNames -Session $Session
        $script:varied = @($names | Where-Object { $_ -match '^varied_obj' })[0]
        Assert-True -Condition ($null -ne $script:varied) -Message 'the varied cloud was placed'
        Remove-StandInMesh -Entity $script:varied
    }
    # The plain cloud out of shot, the varied one in it and close enough that a pixel
    # sees many splats.
    Move-Entity -Entity $script:cloud -Position $Parked
    Move-Entity -Entity $script:varied -Position '0 0 0'
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -2', 'camera_target 0 0 0' | Out-Null

    Step-EditorFrames -Session $Session -Count 12
    $shots = @()
    for ($f = 0; $f -lt 4; $f++) { $shots += (Shot "stable-f$f") }

    $worst = 0.0
    for ($f = 0; $f -lt $shots.Count - 1; $f++) {
        # Over the cloud itself, not the whole window: a share diluted by the empty
        # background and the ImGui panels would pass whatever the cloud did.
        $d = Get-ImageDifference -PathA $shots[$f] -PathB $shots[$f + 1] `
            -Left 0.40 -Top 0.35 -Right 0.60 -Bottom 0.65 -Threshold 8
        if ($d.DifferingShare -gt $worst) { $worst = $d.DifferingShare }
    }
    Move-Entity -Entity $script:varied -Position $Parked
    Move-Entity -Entity $script:cloud -Position '0 0 0'
    Assert-True -Condition ($worst -lt 0.02) `
        -Message ("consecutive frames of a frozen cloud should agree; worst pair " +
                  "differed on $([Math]::Round($worst * 100, 3))% of the cloud")
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

Test 'point_size_scale grows and shrinks the cloud''s screen footprint' {
    # A direct per-splat radius multiplier - it grows or shrinks the silhouette
    # itself. Measured pushed far back, the way max_density's test is, and for a
    # second reason on top of that one's: splat_stats_cpu is one struct, overwritten
    # by whichever cloud RenderSystem::DrawSplats draws last that frame, not indexed
    # by cloud - so with the varied cloud from 'the composite is stable frame to
    # frame' now sitting parked (off screen but still Base.visible) in the same
    # scene, splat_info can report ITS all-zero stats instead of this cloud's
    # whenever it draws second. A screenshot has no such ambiguity: it shows
    # whatever is actually on screen, which is this cloud alone.
    Reset-Cloud
    Move-Entity -Entity $script:cloud -Position '0 0 80'
    $base = Get-CloudShare 'point-size-base'
    # The pass is not temporal (nothing here accumulates between frames - see 'the
    # composite is stable frame to frame' above), so this is a deterministic
    # geometric measurement rather than something with a stochastic floor to clear:
    # a real change reads as a clean ratio, not as noise to out-margin. Asserted as a
    # ratio rather than a percentage-point delta because the baseline itself is a
    # small share of the view at this distance - a fixed point delta tuned for that
    # would either miss a real change here or be too loose to mean anything nearer.
    Assert-True -Condition ($base -gt 0.001) `
        -Message "the baseline should itself be measurable ($([Math]::Round($base * 100, 3))% lit) - recalibrate the distance if this fires"

    SendOk "set_component $($script:cloud) SplatCloud ""{'point_size_scale':8.0}""" | Out-Null
    $grown = Get-CloudShare 'point-size-grown'
    Assert-True -Condition ($grown -gt $base * 1.5) `
        -Message ("a larger point size should cover more of the view " +
                  "($([Math]::Round($base * 100, 3))% -> $([Math]::Round($grown * 100, 3))%)")

    SendOk "set_component $($script:cloud) SplatCloud ""{'point_size_scale':0.2}""" | Out-Null
    $shrunk = Get-CloudShare 'point-size-shrunk'
    Assert-True -Condition ($shrunk -lt $base * 0.67) `
        -Message ("a smaller point size should cover less of the view " +
                  "($([Math]::Round($base * 100, 3))% -> $([Math]::Round($shrunk * 100, 3))%)")

    SendOk "set_component $($script:cloud) SplatCloud ""{'point_size_scale':1.0}""" | Out-Null
    Move-Entity -Entity $script:cloud -Position '0 0 0'
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

#--- point-cloud normals ------------------------------------------------------
# A plain coloured point cloud carries no per-point shape at all, so the minor-axis
# derivation a trained Gaussian's normal comes from has nothing to work with: every
# splat is an isotropic sphere, the tie-break picks the same basis column for all of
# them, and the whole cloud comes out with ONE normal, split into two halves by the
# sign resolution. SplatCloudData::Load fits those normals from the neighbouring
# points instead - the plane through the k nearest, as the smallest eigenvector of
# their covariance - which is the only place the information exists.
#
# The assertions are all on `render debug_buffer normal`, which for a splat shows the
# normal the rasterizer wrote into rt_ray_sources1: the same value the lighting reads,
# not a debug path of its own.

# Imports the point cloud, makes a template and places one, parking the Gaussian cloud
# so the two are never in shot together. Runs once.
function Initialize-PointCloud {
    if ($script:pcloud) { return }
    Initialize-Suite
    $ply = Join-Path $Assets 'Objects\pointcloud.ply'
    New-PointCloudPly -Path $ply
    SendOk "import_model ""$ply""" | Out-Null
    SendOk 'create_template_from_model pointcloud point_obj' | Out-Null
    SendOk 'place point_obj' | Out-Null
    $names = Get-EntityNames -Session $Session
    $script:pcloud = @($names | Where-Object { $_ -match '^point_obj' })[0]
    Assert-True -Condition ($null -ne $script:pcloud) -Message 'the point cloud instance was placed'
    Remove-StandInMesh -Entity $script:pcloud
}

# The point cloud alone, framed to fill the view.
#
# point_size_scale is doing real work here and is not cosmetic. A point cloud has no
# scale_* to read, so every splat falls back to exp(0) = one world unit, twice the
# radius of this whole shell. Left there the model is a single overlapping blob whose
# normals average out to nothing, which would fail these tests with the fit working
# perfectly. 0.07 is about the Gaussian fixture's exp(-2.6), so neighbours overlap
# just enough to read as a surface.
function Reset-PointCloud {
    Initialize-PointCloud
    Move-Entity -Entity $script:cloud -Position $Parked
    SendOk "select $($script:pcloud)", 'set_position 0 0 0', 'set_scale 1 1 1' | Out-Null
    SendOk "set_component $($script:pcloud) SplatCloud ""{'opacity_scale':1.0,'albedo_scale':1.0,'point_size_scale':0.07,'invert_normals':false,'surface_alpha':0.5}""" | Out-Null
    SendOk 'deselect' | Out-Null
    SendOk 'camera_rot 0 0 0', 'camera_pos 0 0 -1.2', 'camera_target 0 0 0' | Out-Null
    SendOk 'render debug_gain 1' | Out-Null
}

# Mean of one channel over a band of the frame, given in fractions of the image.
function Get-Band {
    param([string]$Path, [string]$Channel,
          [double]$Left, [double]$Top, [double]$Right, [double]$Bottom)
    $s = Get-ImageStats -Path $Path -Left $Left -Top $Top -Right $Right -Bottom $Bottom -Step 2
    switch ($Channel) {
        'R' { return $s.MeanR }
        'G' { return $s.MeanG }
        'B' { return $s.MeanB }
    }
}

Test 'a point cloud .ply renders with normals fitted from its neighbours' {
    # The whole point. The normal buffer maps [-1,1] to [0,255], so a normal with no
    # component along an axis reads as 127 on that channel.
    #
    # Before the fit existed, every splat of a point cloud got the same normal - world
    # +X or -X - which is to say the green channel of this buffer was 127 everywhere,
    # on every point cloud, whatever shape it was. On a sphere seen head on the fitted
    # normals must instead sweep with the surface: green high above the equator (the
    # normal tilts up), low below it, and the two far apart.
    Reset-PointCloud
    Step-EditorFrames -Session $Session -Count 8
    SendOk 'render debug_buffer normal' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $shot = Shot 'pointcloud-normal'
    SendOk 'render debug_buffer off' | Out-Null

    $upper = Get-Band -Path $shot -Channel 'G' -Left 0.46 -Top 0.36 -Right 0.54 -Bottom 0.43
    $lower = Get-Band -Path $shot -Channel 'G' -Left 0.46 -Top 0.57 -Right 0.54 -Bottom 0.64
    Assert-True -Condition (($upper - $lower) -gt 30) `
        -Message ("the top of the sphere should face further up than the bottom " +
                  "(green $([Math]::Round($upper, 1)) above, $([Math]::Round($lower, 1)) below)")

    # And the same across, on red. A cloud that came out with one normal for every
    # splat would pass NEITHER; one that kept the old +X/-X split would pass this and
    # fail the green pair above, which is why both are here.
    $left  = Get-Band -Path $shot -Channel 'R' -Left 0.36 -Top 0.46 -Right 0.43 -Bottom 0.54
    $right = Get-Band -Path $shot -Channel 'R' -Left 0.57 -Top 0.46 -Right 0.64 -Bottom 0.54
    Assert-True -Condition (($right - $left) -gt 30) `
        -Message ("the right of the sphere should face further right than the left " +
                  "(red $([Math]::Round($left, 1)) at left, $([Math]::Round($right, 1)) at right)")
}

Test 'point cloud normals are continuous rather than per-point noise' {
    # The other failure this can have, and it is not the one above. The fit gives an
    # AXIS, not a direction, and the sign is resolved by pointing it away from the
    # cloud centroid - which says nothing at all where the surface runs through the
    # centroid, and there the sign comes out of the fit's own error, one point at a
    # time. Neighbouring splats then face opposite ways and the cloud renders as
    # static rather than as a surface.
    #
    # Measured directly: neighbouring pixels of the normal buffer must be near each
    # other. Across a row through the middle of the model the step from one pixel to
    # the next is a few units on a fitted surface and ~128 on a sign that is dithering.
    #
    # Note this one does NOT discriminate the failure the test above catches, and was
    # checked against it: a cloud where every splat shares one normal is perfectly
    # continuous (two regions, one boundary between them) and passes this happily. The
    # two guard different halves - that a normal was fitted at all, and that its sign
    # was resolved consistently - and neither substitutes for the other.
    Reset-PointCloud
    Step-EditorFrames -Session $Session -Count 8
    SendOk 'render debug_buffer normal' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $shot = Shot 'pointcloud-normal-continuity'
    SendOk 'render debug_buffer off' | Out-Null

    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($shot)
    try {
        $y = [int]($bmp.Height * 0.5)
        $x0 = [int]($bmp.Width * 0.42)
        $x1 = [int]($bmp.Width * 0.58)
        $jumps = 0; $steps = 0
        $prev = $bmp.GetPixel($x0, $y)
        for ($x = $x0 + 1; $x -lt $x1; $x++) {
            $p = $bmp.GetPixel($x, $y)
            $d = [Math]::Max([Math]::Abs($p.R - $prev.R),
                 [Math]::Max([Math]::Abs($p.G - $prev.G), [Math]::Abs($p.B - $prev.B)))
            if ($d -gt 60) { $jumps++ }
            $steps++
            $prev = $p
        }
        $share = $jumps / [double]$steps
        Assert-True -Condition ($share -lt 0.15) `
            -Message ("normals across the model should vary smoothly, " +
                      "$([Math]::Round($share * 100, 1))% of neighbouring pixels jump")
    }
    finally {
        $bmp.Dispose()
    }
}

Test 'invert_normals still flips a fitted point cloud' {
    # The escape hatch for the half of the orientation nothing local can decide: a
    # room scanned from inside comes out with every normal facing the wall. It acts on
    # a fitted normal exactly as it does on a Gaussian's minor axis, so the buffer must
    # come back mirrored about mid-grey rather than merely different.
    Reset-PointCloud
    Step-EditorFrames -Session $Session -Count 8
    SendOk 'render debug_buffer normal' | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $before = Get-Band -Path (Shot 'pointcloud-normal-out') -Channel 'G' `
                       -Left 0.46 -Top 0.36 -Right 0.54 -Bottom 0.43

    SendOk "set_component $($script:pcloud) SplatCloud ""{'invert_normals':true}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 6
    $after = Get-Band -Path (Shot 'pointcloud-normal-in') -Channel 'G' `
                      -Left 0.46 -Top 0.36 -Right 0.54 -Bottom 0.43
    SendOk "set_component $($script:pcloud) SplatCloud ""{'invert_normals':false}""" | Out-Null
    SendOk 'render debug_buffer off' | Out-Null

    Assert-True -Condition ((($before - 127.5) * ($after - 127.5)) -lt 0) `
        -Message ("inverting should put the normal on the other side of mid-grey " +
                  "(green $([Math]::Round($before, 1)) -> $([Math]::Round($after, 1)))")
    Move-Entity -Entity $script:pcloud -Position $Parked
}
