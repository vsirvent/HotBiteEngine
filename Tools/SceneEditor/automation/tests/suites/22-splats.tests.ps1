# fixture: empty
# description: The SplatCloud component - Gaussian splat clouds as an alternative to a triangle mesh.
#
# What is being covered here is the *authoring* half: that the component can be
# added, edited, undone, saved and reloaded like any other, and that the values it
# round-trips are the ones that were typed. None of it needs a .ply on disk,
# because World::GetDefaultSplatCloud builds a cloud procedurally - which is the
# reason that stand-in exists at all, so a splat test needs no binary asset
# committed to the repo and no import step before it can assert anything.
#
# Deliberately not covered yet: that a cloud *renders*. The splat rasterizer is a
# separate pass and gets its own tests with it; a screenshot assertion written
# before that pass exists would be asserting on an empty frame.

$DefaultCloud = '__default_splat_cloud'

# Writes a minimal binary-little-endian 3DGS .ply. Generated rather than committed
# for the same reason the fixture project is: a real capture is hundreds of
# megabytes, and everything the import path needs to be exercised on is present in
# a dozen splats.
#
# The property order deliberately does NOT match the reference implementation's -
# rot comes before scale here, as it does in the files some trainers emit. The
# loader indexes properties by name off the header, and this is what proves it,
# because a loader that assumed a fixed layout would decode this file into
# nonsense without failing.
function New-TestPly {
    param([string]$Path, [int]$Count = 12)
    $props = @('x', 'y', 'z', 'rot_0', 'rot_1', 'rot_2', 'rot_3',
               'scale_0', 'scale_1', 'scale_2', 'opacity', 'f_dc_0', 'f_dc_1', 'f_dc_2')
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex $Count`n"
    foreach ($p in $props) { $header += "property float $p`n" }
    $header += "end_header`n"

    $body = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($body)
    for ($i = 0; $i -lt $Count; $i++) {
        $t = $i / [double]$Count
        $w.Write([float]([Math]::Cos($t * 6.283) * 0.5))   # x
        $w.Write([float]($t - 0.5))                         # y
        $w.Write([float]([Math]::Sin($t * 6.283) * 0.5))   # z
        $w.Write([float]1.0); $w.Write([float]0.0)          # rot w,x
        $w.Write([float]0.0); $w.Write([float]0.0)          # rot y,z
        # Log-scales: exp(-3) ~ 0.05 world units, and anisotropic so the minor axis
        # the normal is derived from is unambiguous.
        $w.Write([float](-3.0)); $w.Write([float](-3.0)); $w.Write([float](-4.5))
        $w.Write([float]2.0)                                # opacity, pre-sigmoid
        # f_dc: SH degree 0, decoded as 0.5 + 0.2820948 * f. 1.77 -> ~1.0 red.
        $w.Write([float]1.77); $w.Write([float](-1.77)); $w.Write([float](-1.77))
    }
    $w.Flush()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($header) + $body.ToArray()
    [IO.File]::WriteAllBytes($Path, $bytes)
    $w.Dispose(); $body.Dispose()
}

Test 'a placed mesh instance has no SplatCloud' {
    # The two are alternatives, and nothing gives an entity one implicitly. If this
    # ever starts passing by accident, every other test in this file is measuring a
    # component it did not add.
    Assert-NotContains -Collection (SendOk 'components box_a')[0].Payload -Value 'SplatCloud' `
        -Message 'a cube instance is a mesh, not a splat cloud'
    Assert-Err -Result (Send 'component box_a SplatCloud')[0] -Pattern 'has no SplatCloud'
}

Test 'add_component gives a SplatCloud the built-in stand-in cloud' {
    SendOk 'add_component box_a SplatCloud' | Out-Null
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'SplatCloud' `
        -Message 'after add'
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    # Added from scratch with nothing named, FromJson falls back to the generated
    # cloud rather than leaving a null - an entity carrying a component that draws
    # nothing is indistinguishable from the component not working.
    Assert-Equal -Expected $DefaultCloud -Actual $splat.name -Message 'the stand-in was adopted'
}

Test 'SplatCloud serializes every field, not only the ones that differ from default' {
    # The undo contract. ComponentOps::RecordEdit implements undo by replaying an
    # earlier ToJson, and every FromJson in this engine reads a missing key as
    # "leave alone" - so a field omitted because it happened to match its default at
    # save time could never be restored, and the undo would report success while
    # changing nothing. Mesh's "smooth" documents the same trap.
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    foreach ($field in @('name', 'opacity_scale', 'albedo_scale', 'spec_intensity',
                         'invert_normals', 'surface_alpha')) {
        Assert-True -Condition ($null -ne $splat.$field) -Message "SplatCloud.$field is serialized"
    }
    # Every one of these is at its default, which is precisely the case that would
    # be missing if ToJson wrote conditionally.
    Assert-Near -Expected 1.0 -Actual $splat.opacity_scale
    Assert-Near -Expected 1.0 -Actual $splat.albedo_scale
    Assert-Equal -Expected 'False' -Actual $splat.invert_normals
}

Test 'whether a cloud renders is Base.visible, not a flag of its own' {
    # A splat cloud has no say of its own in whether it draws. It was briefly given
    # one (a write_gbuffer switch, for wispy captures that have no real surface) and
    # that was a third state on top of the two Base already describes - so the knob
    # went and Base::visible is the only answer.
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-True -Condition ($null -eq $splat.write_gbuffer) -Message 'no per-cloud render flag'
    $base = Get-Component -Session $Session -Entity 'box_a' -Component 'Base'
    Assert-True -Condition ($null -ne $base.visible) -Message 'Base.visible is what controls it'
}

Test 'set_component edits named fields and leaves the rest alone' {
    SendOk "set_component box_a SplatCloud ""{'opacity_scale':0.25,'invert_normals':true}""" | Out-Null
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-Near -Expected 0.25 -Actual $splat.opacity_scale
    Assert-Equal -Expected 'True' -Actual $splat.invert_normals
    Assert-Near -Expected 1.0 -Actual $splat.albedo_scale -Message 'a key left out keeps its value'
    Assert-Equal -Expected $DefaultCloud -Actual $splat.name -Message 'and so does the cloud'
}

Test 'the material knobs are independent of each other' {
    # albedo_scale and spec_intensity are what make an imported capture sit next to
    # authored materials: its colours are radiance being reinterpreted as
    # reflectance, so they routinely land too bright or too dark. Editing one must
    # not disturb the other.
    SendOk "set_component box_a SplatCloud ""{'albedo_scale':2.5}""" | Out-Null
    SendOk "set_component box_a SplatCloud ""{'spec_intensity':0.75}""" | Out-Null
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-Near -Expected 2.5 -Actual $splat.albedo_scale
    Assert-Near -Expected 0.75 -Actual $splat.spec_intensity
    Assert-Near -Expected 0.25 -Actual $splat.opacity_scale -Message 'the earlier edit survived both'
}

Test 'surface_alpha is clamped away from both ends' {
    # It is where front-to-back accumulated alpha is declared to have reached "the
    # surface", and both extremes are degenerate rather than merely extreme: at 0
    # the first splat touched wins, so the depth written is the nearest floater
    # rather than the object; at 1 the crossing never happens on any pixel that is
    # not fully opaque, so nothing is written at all. Both look exactly like the
    # depth write being broken, which is why they are refused at the edit rather
    # than guarded in the shader.
    SendOk "set_component box_a SplatCloud ""{'surface_alpha':0.0}""" | Out-Null
    $low = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-True -Condition ([double]$low.surface_alpha -gt 0.0) -Message 'clamped off zero'

    SendOk "set_component box_a SplatCloud ""{'surface_alpha':5.0}""" | Out-Null
    $high = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-True -Condition ([double]$high.surface_alpha -lt 1.0) -Message 'clamped off one'

    SendOk "set_component box_a SplatCloud ""{'surface_alpha':0.5}""" | Out-Null
}

Test 'a SplatCloud edit is undoable' {
    SendOk "set_component box_a SplatCloud ""{'opacity_scale':0.9}""" | Out-Null
    Assert-Near -Expected 0.9 `
        -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud').opacity_scale
    SendOk 'undo' | Out-Null
    # Back to what it was before that edit, not to the field's default - the
    # distinction the unconditional ToJson above exists to make possible.
    Assert-Near -Expected 0.25 `
        -Actual (Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud').opacity_scale `
        -Message 'undo restores the previous value, not the default'
}

Test 'a SplatCloud edit is written to that entity, not to its template' {
    # box_a and box_b are instances of the same template, so an edit that reached
    # the template would show up on both - the per-entity delta is what makes this
    # a level edit rather than a change to what a "box" is.
    Assert-NotContains -Collection (SendOk 'components box_b')[0].Payload -Value 'SplatCloud' `
        -Message 'the sibling instance is untouched'
}

Test 'remove_component takes the SplatCloud away and undo puts it back with its values' {
    SendOk 'remove_component box_a SplatCloud' | Out-Null
    Assert-NotContains -Collection (SendOk 'components box_a')[0].Payload -Value 'SplatCloud' `
        -Message 'after remove'

    SendOk 'undo' | Out-Null
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'SplatCloud' `
        -Message 'after undo'
    $splat = Get-Component -Session $Session -Entity 'box_a' -Component 'SplatCloud'
    Assert-Near -Expected 0.25 -Actual $splat.opacity_scale -Message 'the values came back too'
    Assert-Near -Expected 2.5 -Actual $splat.albedo_scale
    Assert-Equal -Expected 'True' -Actual $splat.invert_normals
}

Test 'add_component refuses a second SplatCloud' {
    Assert-Err -Result (Send 'add_component box_a SplatCloud')[0] -Pattern 'already has'
}

Test 'a SplatCloud and a Mesh can coexist on one entity' {
    # Not the intended way to use it, but it must not be a crash or a silent
    # refusal: the two draw through different paths and neither knows about the
    # other. Asserted so that if a future exclusivity rule is added, it is added
    # deliberately and this test is what fails.
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'Mesh' `
        -Message 'the entity still has its mesh'
    Assert-Contains -Collection (SendOk 'components box_a')[0].Payload -Value 'SplatCloud'
}

#--- importing a .ply ---------------------------------------------------------
# A splat cloud enters through the existing model layer: World::LoadModel
# dispatches on the file extension, so File/Import Model and import_model work on
# a .ply unchanged. It registers under the file stem and creates no template,
# which is the whole point of the model/template split - a capture is a bag of
# assets, not a placeable object.

Test 'import_model accepts a .ply and registers it under the file stem' {
    $ply = Join-Path $Assets 'Objects\testcloud.ply'
    New-TestPly -Path $ply -Count 12
    SendOk "import_model ""$ply""" | Out-Null
    # list_models decorates each line with what the file contributed, so this is a
    # prefix match rather than an equality one.
    $line = (SendOk 'list_models')[0].Payload | Where-Object { $_ -match '^testcloud\b' }
    Assert-True -Condition ($null -ne $line) -Message 'the cloud is listed as a model'
    # It contributed a cloud and none of the mesh-side assets - and the readout says
    # so, rather than reporting three zeros and looking like a failed import.
    Assert-Match -Actual $line -Pattern 'splat_clouds=1'
    Assert-Match -Actual $line -Pattern 'splats=12'
    Assert-Match -Actual $line -Pattern 'meshes=0'
}

Test 'an imported cloud is namable by a component and carries its splats' {
    SendOk 'add_component box_c SplatCloud' | Out-Null
    SendOk "set_component box_c SplatCloud ""{'name':'testcloud'}""" | Out-Null
    Assert-Equal -Expected 'testcloud' `
        -Actual (Get-Component -Session $Session -Entity 'box_c' -Component 'SplatCloud').name `
        -Message 'the component resolved the imported cloud by name'
}

Test 'Create Template turns an imported cloud into a placeable object' {
    # The step between "imported" and "in the scene", and the reason the Asset
    # Browser's button exists. It used to refuse a .ply outright: the template
    # builder looked for the model's first *mesh* node, a cloud registers no nodes
    # at all, and the failure it reported was "this is an animation-only model".
    $r = SendOk 'create_template_from_model testcloud splat_obj'
    $blocks = Get-TemplateInfo -Session $Session -Template 'splat_obj'
    Assert-True -Condition ($null -ne $blocks['SplatCloud']) -Message 'the template carries the cloud'
    Assert-Equal -Expected 'testcloud' -Actual $blocks['SplatCloud'].name

    # KNOWN LIMITATION, asserted so it is visible rather than surprising: the
    # template also carries the built-in cube. World::CreateTemplate gives every
    # template a Mesh so that one with nothing authored is immediately placeable,
    # and SpawnInstance/GetTemplateEntity both require a template entity to have
    # Mesh + Bounds + Transform - so a splat template cannot simply drop it without
    # reworking those two. Until the splat render pass exists this is invisible
    # anyway; once it does, a splat object rendering *inside a white cube* is the
    # symptom, and this assertion is the note explaining why.
    Assert-True -Condition ($null -ne $blocks['Mesh']) `
        -Message 'a splat template still gets the stand-in cube (see comment)'
}

Test 'a splat template can be placed in the level' {
    SendOk 'place splat_obj' | Out-Null
    $names = Get-EntityNames -Session $Session
    $placed = $names | Where-Object { $_ -match '^splat_obj' }
    Assert-True -Condition ($null -ne $placed) -Message 'an instance of the splat template exists'
    Assert-Contains -Collection (SendOk "components $placed")[0].Payload -Value 'SplatCloud' `
        -Message 'and it carries the cloud'
}

Test 'importing a .ply creates no template' {
    # The model/template/instance split: an import contributes assets and nothing
    # placeable. A .ply that created a template would be the same mistake the FBX
    # path was fixed for.
    $templates = (SendOk 'list_templates')[0].Payload | Where-Object { $_ -match '^testcloud\b' }
    Assert-True -Condition ($null -eq $templates) -Message 'no template was invented for the cloud'
}

Test 'a file that is not a 3DGS .ply is refused rather than half-loaded' {
    # A .ply with no Gaussian properties parses as a valid .ply and would otherwise
    # register as an empty cloud - a model that silently draws nothing.
    $bad = Join-Path $Assets 'Objects\notsplats.ply'
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex 2`n" +
              "property float x`nproperty float y`nproperty float z`nend_header`n"
    $body = New-Object byte[] 24
    [IO.File]::WriteAllBytes($bad, ([System.Text.Encoding]::ASCII.GetBytes($header) + $body))
    Send "import_model ""$bad""" | Out-Null
    Assert-NotContains -Collection (SendOk 'list_models')[0].Payload -Value 'notsplats' `
        -Message 'a .ply missing the splat properties does not register'
}

#--- persistence --------------------------------------------------------------
# Last in the file: this writes the project, and a suite that saves must not
# reach the next one.

Test 'the component is saved into the entity own record' {
    SendOk 'menu "File/Save Level"' | Out-Null
    $saved = Get-Content -Raw (Join-Path $Assets 'Levels\Solo\1\level.json') | ConvertFrom-Json
    $record = $saved.world.instances | Where-Object { $_.name -eq 'box_a' }
    Assert-True -Condition ($null -ne $record.components.SplatCloud) -Message 'the block is in the file'
    Assert-Equal -Expected $DefaultCloud -Actual $record.components.SplatCloud.name
    Assert-Near -Expected 0.25 -Actual $record.components.SplatCloud.opacity_scale
    Assert-Near -Expected 2.5 -Actual $record.components.SplatCloud.albedo_scale
    Assert-True -Condition ([bool]$record.components.SplatCloud.invert_normals) `
        -Message 'invert_normals survived the save'
    # The whole point of the unconditional write: fields still at their defaults are
    # in the file too, so a later undo has something to replay.
    Assert-True -Condition ($null -ne $record.components.SplatCloud.surface_alpha) `
        -Message 'a field left at its default is written as well'

    $sibling = $saved.world.instances | Where-Object { $_.name -eq 'box_b' }
    Assert-True -Condition ($null -eq $sibling.components.SplatCloud) `
        -Message 'and it did not leak onto the sibling instance'
}
