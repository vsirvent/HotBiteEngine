# fixture: empty
# description: A splat cloud's inferred low-poly proxy (World::GenerateSplatProxy) -
# the mesh/shape a cloud needs to cast a shadow or carry a Physics component, since
# the compute-based splat renderer never touches the Mesh/Material draw trees a
# shadow or a collider is built from. Regenerating (a new resolution/ratio) and
# removing it are both first-class operations here, not just the initial build.

# A sphere shell of ANISOTROPIC splats (flattened radially, like New-TestPly's ring),
# each one's rotation aligning its flattened axis with the true outward normal at its
# position - unlike 23-splatrender's New-RenderPly (isotropic, rotation always
# identity), which is fine for coverage/binning tests but gives
# SplatCloudData::Load's minor-axis normal derivation nothing well-defined to find.
# The reconstruction here is Hoppe's oriented-point signed distance, so it needs real
# normals to find a real surface.
function New-SplatSpherePly {
    param([string]$Path, [int]$Count = 700, [double]$Radius = 1.0)
    $props = @('x', 'y', 'z', 'rot_0', 'rot_1', 'rot_2', 'rot_3',
               'scale_0', 'scale_1', 'scale_2', 'opacity', 'f_dc_0', 'f_dc_1', 'f_dc_2')
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex $Count`n"
    foreach ($p in $props) { $header += "property float $p`n" }
    $header += "end_header`n"

    $body = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($body)
    $golden = 2.39996322972865332
    for ($i = 0; $i -lt $Count; $i++) {
        # Fibonacci sphere: a unit outward direction (nx,ny,nz) per point, uniform
        # over the shell with no pole clustering.
        $ny = 1.0 - 2.0 * ($i + 0.5) / $Count
        $r = [Math]::Sqrt([Math]::Max(0.0, 1.0 - $ny * $ny))
        $theta = $golden * $i
        $nx = [Math]::Cos($theta) * $r
        $nz = [Math]::Sin($theta) * $r

        $w.Write([float]($nx * $Radius))
        $w.Write([float]($ny * $Radius))
        $w.Write([float]($nz * $Radius))

        # Shortest-arc quaternion rotating +Z to (nx,ny,nz) - cross(+Z,n),1+dot(+Z,n),
        # normalized - so the flattened axis below (scale_2, the smallest) points
        # radially outward, which is the minor axis SplatCloudData::Load reads as the
        # per-splat normal for a trained-Gaussian-shaped .ply.
        if ($nz -lt -0.9999999) {
            $qw = 0.0; $qx = 1.0; $qy = 0.0; $qz = 0.0
        }
        else {
            $qx = -$ny; $qy = $nx; $qz = 0.0; $qw = 1.0 + $nz
            $len = [Math]::Sqrt($qx * $qx + $qy * $qy + $qz * $qz + $qw * $qw)
            $qx /= $len; $qy /= $len; $qz /= $len; $qw /= $len
        }
        $w.Write([float]$qw); $w.Write([float]$qx); $w.Write([float]$qy); $w.Write([float]$qz)
        # Flattened along local Z (most negative log-scale): a thin shell splat, same
        # magnitude as New-RenderPly's so neighbours overlap into a surface.
        $w.Write([float](-2.6)); $w.Write([float](-2.6)); $w.Write([float](-4.5))
        $w.Write([float]4.0)                                # opacity, pre-sigmoid: ~0.98
        $w.Write([float]1.77); $w.Write([float]1.77); $w.Write([float]1.77)
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

# The collider-vs-mesh comparison line physics_info reports for the selected entity -
# "n/a" for a primitive shape, "ok"/"SUSPECT" for a mesh one. See 13-physics.tests.ps1
# for the same reading, duplicated here rather than shared since it is a one-line parse.
function Get-ColliderVerdict {
    $r = SendOk 'physics_info'
    $line = $r[0].Payload | Where-Object { $_ -like '*collider_smaller_than_mesh_by=*' }
    return $line
}

$Sphere = Join-Path $ShotDir 'splat_sphere.ply'
New-SplatSpherePly -Path $Sphere -Count 700 -Radius 1.0
SendOk "import_model ""$Sphere"" splat_sphere" | Out-Null

function New-BareSplatEntity {
    param([string]$CloudName = 'splat_sphere')
    SendOk 'menu "Add/Entity"' | Out-Null
    $entity = (Get-State -Session $Session).selected_entity_name
    SendOk "add_component $entity SplatCloud" | Out-Null
    SendOk "set_component $entity SplatCloud ""{'name':'$CloudName'}""" | Out-Null
    return $entity
}

Test 'generate_splat_proxy needs a selection' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'generate_splat_proxy')[0] -Pattern 'nothing selected'
}

Test 'generate_splat_proxy refuses an entity with no SplatCloud' {
    SendOk 'menu "Add/Entity"' | Out-Null
    Assert-Err -Result (Send 'generate_splat_proxy')[0] -Pattern 'has no SplatCloud'
}

Test 'generate_splat_proxy builds a mesh with a plausible triangle count' {
    $script:SplatEntity = New-BareSplatEntity
    SendOk "select $script:SplatEntity" | Out-Null
    $r = SendOk 'generate_splat_proxy 40 40'
    Assert-Match -Pattern '^splat_sphere_proxy ' -Actual $r[0].Text
    Assert-Match -Pattern 'vertices=\d+' -Actual $r[0].Text
    Assert-Match -Pattern 'indices=\d+' -Actual $r[0].Text
    if ($r[0].Text -notmatch '^(\S+) vertices=(\d+)') { throw "unexpected response: $($r[0].Text)" }
    $script:SplatProxyName = $Matches[1]
    $vertices = [int]$Matches[2]
    Assert-Equal -Expected 'splat_sphere_proxy' -Actual $script:SplatProxyName -Message 'the first generation keeps the plain name'
    # A sphere shell reconstructed and decimated toward 40%: not degenerate (a
    # handful of stray triangles) and not the raw reconstruction's full vertex count.
    Assert-True -Condition ($vertices -gt 20) -Message 'not a degenerate sliver'
    Assert-True -Condition ($vertices -lt 5000) -Message 'actually decimated, not the raw mesh'
}

Test 'the generated mesh is attached as a shadow-caster-only Mesh+Bounds' {
    $entity = $script:SplatEntity
    $components = (SendOk "components $entity")[0].Payload
    Assert-Contains -Collection $components -Value 'Mesh' -Message 'the proxy mesh'
    Assert-Contains -Collection $components -Value 'Bounds' -Message 'sized from it'
    Assert-Contains -Collection $components -Value 'Material' -Message 'so it can join shadow_tree'
    Assert-Contains -Collection $components -Value 'SplatCloud' -Message 'still a splat cloud too'

    $mesh = Get-Component -Session $Session -Entity $entity -Component 'Mesh'
    Assert-Equal -Expected $script:SplatProxyName -Actual $mesh.name

    $base = Get-Component -Session $Session -Entity $entity -Component 'Base'
    Assert-True -Condition $base.shadow_caster_only -Message 'never drawn in the color pass'
}

Test 'the reconstructed bounds are roughly the sphere it was built from' {
    $entity = $script:SplatEntity
    $bounds = Get-Component -Session $Session -Entity $entity -Component 'Bounds'
    $extent_x = $bounds.extents.x
    $extent_y = $bounds.extents.y
    $extent_z = $bounds.extents.z
    # A unit-radius sphere shell, plus a bit for splat thickness and the grid's own
    # coarseness - loose bounds, since this is only a sanity check that the surface
    # was reconstructed at all and not, say, collapsed to a point or blown out to the
    # padded grid's full extent.
    foreach ($extent in @($extent_x, $extent_y, $extent_z)) {
        Assert-True -Condition ($extent -gt 0.5 -and $extent -lt 2.0) `
            -Message "extent $extent is roughly the unit sphere"
    }
}

Test 'the scene still renders after a proxy is attached' {
    # Not a visual assertion - just that a shadow-caster-only entity does not corrupt
    # the frame (the risk RenderSystem::NotifySignatureChange's gate exists to avoid:
    # see Base::shadow_caster_only's own comment on why it must skip the depth
    # pre-pass too).
    SendOk 'camera_pos 0 0 -4', 'camera_target 0 0 0' | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $shot = Join-Path $ShotDir 'proxy_scene.png'
    SendOk "screenshot ""$shot""" | Out-Null
    Assert-FileExists -Path $shot
}

Test 'a STATIC Physics component collides against the real reconstructed shape' {
    $entity = $script:SplatEntity
    SendOk "select $entity" | Out-Null
    SendOk "add_component $entity Physics" | Out-Null
    SendOk "set_component $entity Physics ""{'type':'STATIC'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $verdict = Get-ColliderVerdict
    Assert-Match -Pattern 'ok$' -Actual $verdict `
        -Message "a mesh collider built from the same points as Bounds should track it: $verdict"
}

Test 'regenerating with a live STATIC collider does not crash and swaps to a new mesh' {
    # This is the exact scenario that used to bring the editor down: a live
    # reactphysics3d collider was still holding TriangleVertexArray pointers
    # straight into the previous generation's Core::ShapeData when
    # GenerateSplatProxy overwrote that data in place. It now always mints a new
    # mesh/shape pair instead - see World::GenerateSplatProxy's own comment.
    $entity = $script:SplatEntity
    SendOk "select $entity" | Out-Null
    $before = $script:SplatProxyName
    $r = SendOk 'generate_splat_proxy 24 60'
    if ($r[0].Text -notmatch '^(\S+) vertices=(\d+)') { throw "unexpected response: $($r[0].Text)" }
    $script:SplatProxyName = $Matches[1]
    Assert-True -Condition ($script:SplatProxyName -ne $before) -Message 'a new name, not a reuse of the old one'
    Assert-Equal -Expected 'splat_sphere_proxy2' -Actual $script:SplatProxyName

    # Still alive, and the entity picked up the new mesh and re-tracks it exactly -
    # both would fail if the collider had been left referencing freed/rewritten data.
    Assert-Ok -Result (Send 'ping')[0] -Message 'the editor is still responding'
    $mesh = Get-Component -Session $Session -Entity $entity -Component 'Mesh'
    Assert-Equal -Expected $script:SplatProxyName -Actual $mesh.name
    $verdict = Get-ColliderVerdict
    Assert-Match -Pattern 'ok$' -Actual $verdict -Message 'the live collider swapped to the new shape'
}

Test 'regenerating a second time is just as stable' {
    # Once is not enough to trust: the crash above was intermittent-looking in
    # practice (it depended on when reactphysics3d's narrow phase next touched the
    # freed data), so this repeats it once more.
    $entity = $script:SplatEntity
    SendOk "select $entity" | Out-Null
    $r = SendOk 'generate_splat_proxy 60 25'
    if ($r[0].Text -notmatch '^(\S+) vertices=(\d+)') { throw "unexpected response: $($r[0].Text)" }
    $script:SplatProxyName = $Matches[1]
    Assert-Equal -Expected 'splat_sphere_proxy3' -Actual $script:SplatProxyName
    Assert-Ok -Result (Send 'ping')[0]
    $verdict = Get-ColliderVerdict
    Assert-Match -Pattern 'ok$' -Actual $verdict
}

Test 'a DYNAMIC body on the same entity falls back to a primitive, not the mesh' {
    # reactphysics3d concave shapes are STATIC/KINEMATIC only - World::Init/
    # Physics::FromJson only look up GetEntityShape for a STATIC body, by design.
    $entity = $script:SplatEntity
    SendOk "select $entity" | Out-Null
    SendOk "set_component $entity Physics ""{'type':'DYNAMIC','shape':'SPHERE'}""" | Out-Null
    Step-EditorFrames -Session $Session -Count 2
    $verdict = Get-ColliderVerdict
    Assert-Match -Pattern 'n/a' -Actual $verdict -Message 'primitive shapes are not compared to the mesh'
    # Back to STATIC so the tests that follow see the mesh collider again.
    SendOk "set_component $entity Physics ""{'type':'STATIC'}""" | Out-Null
}

Test 'a generated proxy is written to the level and survives a reload' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $level = Get-Content $LevelPath -Raw | ConvertFrom-Json
    Assert-True -Condition ($null -ne $level.world.generated_splat_proxies) -Message 'the recipe is saved'
    $recipe = $level.world.generated_splat_proxies | Where-Object { $_.source -eq 'splat_sphere' }
    Assert-True -Condition ($null -ne $recipe) -Message 'keyed by the cloud it came from'
    # Only the latest generation's recipe - the superseded ones from the two
    # regenerates above are not carried along (GenerateSplatProxy drops the old
    # entry for the cloud before appending the new one).
    Assert-Equal -Expected $script:SplatProxyName -Actual $recipe.name
    $recipe = @($level.world.generated_splat_proxies | Where-Object { $_.source -eq 'splat_sphere' })
    Assert-Equal -Expected 1 -Actual $recipe.Count -Message 'exactly one recipe per cloud'

    $reloadDir = Join-Path (Split-Path -Parent $ShotDir) 'reload-splat-proxy'
    $reloaded = New-EditorSession -Exe $Session.Exe -Level $LevelPath -AutomationDir $reloadDir
    try {
        $components = (Invoke-EditorCommand -Session $reloaded -Command "components $script:SplatEntity")[0].Payload
        Assert-Contains -Collection $components -Value 'Mesh' -Message 'the proxy reattached on load'
        $mesh = Get-Component -Session $reloaded -Entity $script:SplatEntity -Component 'Mesh'
        Assert-Equal -Expected $script:SplatProxyName -Actual $mesh.name
    }
    finally {
        Close-EditorSession -Session $reloaded
    }
}

Test 'remove_splat_proxy needs a selection' {
    SendOk 'deselect' | Out-Null
    Assert-Err -Result (Send 'remove_splat_proxy')[0] -Pattern 'nothing selected'
    SendOk "select $script:SplatEntity" | Out-Null
}

Test 'remove_splat_proxy refuses an entity with no SplatCloud' {
    SendOk 'menu "Add/Entity"' | Out-Null
    Assert-Err -Result (Send 'remove_splat_proxy')[0] -Pattern 'has no SplatCloud'
    SendOk "select $script:SplatEntity" | Out-Null
}

Test 'remove_splat_proxy takes the components away and physics falls back to a primitive' {
    $entity = $script:SplatEntity
    SendOk "select $entity" | Out-Null
    $r = SendOk 'remove_splat_proxy'
    Assert-Match -Pattern 'removed splat proxy' -Actual $r[0].Text

    $components = (SendOk "components $entity")[0].Payload
    Assert-NotContains -Collection $components -Value 'Mesh' -Message 'the proxy mesh is gone'
    Assert-NotContains -Collection $components -Value 'Bounds' -Message 'and what it was sized from'
    Assert-NotContains -Collection $components -Value 'Material' -Message 'and what let it join shadow_tree'
    Assert-Contains -Collection $components -Value 'SplatCloud' -Message 'still a splat cloud'
    Assert-Contains -Collection $components -Value 'Physics' -Message 'the component itself is left alone'

    $base = Get-Component -Session $Session -Entity $entity -Component 'Base'
    Assert-False -Condition $base.shadow_caster_only -Message 'no longer suppressed from the color pass'

    # The STATIC body survives (Physics itself was never removed) but has nothing
    # left to build a mesh collider from, so it falls back to the primitive form.
    # physics_info's mesh_aabb/collider_smaller_than_mesh_by lines are themselves
    # gated on the entity still having a Bounds, which is exactly what removal just
    # took away - so unlike the DYNAMIC case (which keeps its Bounds and gets an
    # explicit "n/a" verdict line), the read here is shape_data=none on the main line.
    Step-EditorFrames -Session $Session -Count 2
    $line = (SendOk 'physics_info')[0].Payload | Where-Object { $_ -like "$entity body=*" }
    Assert-Match -Pattern 'shape_data=none' -Actual $line -Message 'no mesh left for a STATIC body to collide against'

    # Generating again works from a clean slate - proves removal did not leave the
    # world's registries (meshes/shapes/generated_splat_proxies) in a state that
    # would make a fresh generate_splat_proxy fail or collide with a stale name.
    $r = SendOk 'generate_splat_proxy 20 50'
    Assert-Match -Pattern '^splat_sphere_proxy4 ' -Actual $r[0].Text -Message 'names keep incrementing, no reuse'
}
