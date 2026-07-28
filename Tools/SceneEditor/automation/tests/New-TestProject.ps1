# Generates a throwaway HotBite project for the automation tests to run against.
#
# The tests deliberately do not use Tests/DemoGame: that level takes tens of
# seconds to load, its contents drift with the demo, and half the suites write to
# the project (save level, save templates, save materials, import). A generated
# project is fast, is described entirely by this file - so an assertion on "3
# boxes" has something to point at - and is thrown away afterwards.
#
# Two kinds:
#   empty   lights, a camera rig and three cube instances. No FBX at all, because
#           World::CreateTemplate turns an absent Mesh into the built-in cube -
#           which is enough for entities, selection, transforms, components,
#           groups, clipboard, history, materials, templates, parts, camera,
#           render settings and persistence.
#   models  the same plus the demo troll (a skinned mesh with clips), which is
#           what animation, bone sockets, skinned bounds and mesh-collider tests
#           need. Costs ~13 MB of copying and a few seconds of FBX load.
#
# Both write absolute paths, so the project can live anywhere (World::Load
# resolves a relative "path" against the process working directory, which for the
# editor is the build output folder, not the project).

param(
    [ValidateSet('empty', 'models')][string]$Kind = 'empty',
    [Parameter(Mandatory = $true)][string]$Path,
    # Where the troll assets are copied from for -Kind models.
    [string]$TrollAssets = (Join-Path $PSScriptRoot '..\..\..\..\Tests\DemoGame\assets\troll')
)

$ErrorActionPreference = 'Stop'

function Write-Json {
    param([string]$File, $Value, [int]$Depth = 20)
    $json = $Value | ConvertTo-Json -Depth $Depth
    [IO.File]::WriteAllText($File, $json, (New-Object Text.UTF8Encoding($false)))
}

$root = [IO.Path]::GetFullPath($Path)
$assets = Join-Path $root 'Assets'
foreach ($d in @($assets,
                 (Join-Path $assets 'Objects'),
                 (Join-Path $assets 'Templates'),
                 (Join-Path $assets 'Ui'),
                 (Join-Path $assets 'materials'),
                 (Join-Path $assets 'Levels\Solo\1'))) {
    New-Item -ItemType Directory -Force $d | Out-Null
}

# config.json is what ProjectBrowser::DeriveProjectRoot walks up to find, so it
# is what makes $root the project root rather than the level's own folder.
Write-Json -File (Join-Path $root 'config.json') -Value @{
    ui      = @{ root = 'Assets\Ui\' }
    objects = @{ root = 'Assets\Objects\' }
    solo    = @{ root = 'Assets\Levels\Solo\'; levels = @(@{ id = 1; name = 'Level 1' }) }
}

#--- materials ----------------------------------------------------------------
# One .mat file with two materials: create_material needs an existing file to
# write into, remove_material needs a material with users, and set_material needs
# something to switch between.
$standardShaders = @{
    draw_vs   = 'MainRenderVS.cso'
    draw_hs   = 'MainRenderHS.cso'
    draw_ds   = 'MainRenderDS.cso'
    draw_gs   = 'MainRenderGS.cso'
    draw_ps   = 'MainRenderPS.cso'
    shadow_vs = 'ShadowVS.cso'
    shadow_gs = 'ShadowMapCubeGS.cso'
    depth_vs  = 'DepthVS.cso'
    depth_ps  = 'DepthPS.cso'
}
function New-MaterialRecord {
    param([string]$Name, [string]$Diffuse)
    $m = @{
        name             = $Name
        diffuse_color    = $Diffuse
        ambient_color    = '#000000FF'
        emission_color   = '#000000FF'
        alpha_enabled    = $false
        blend_enabled    = $false
        opacity          = 1.0
        density          = 1.0
        specular         = 0.2
        emission         = 0.0
        bloom_scale      = 0.0
        raytrace         = $true
        rt_reflex        = 0.2
        tess_factor      = 0.0
        tess_type        = 0
        parallax_scale   = 0.0
        parallax_steps   = 4.0
        parallax_angle_steps = 5.0
        displacement_scale = 0.0
        normal_map_enabled = $false
    }
    foreach ($k in $standardShaders.Keys) { $m[$k] = $standardShaders[$k] }
    return $m
}
Write-Json -File (Join-Path $assets 'materials\test.mat') -Value @{
    root      = 'materials\'
    materials = @((New-MaterialRecord -Name 'TestRed'  -Diffuse '#FF4030FF'),
                  (New-MaterialRecord -Name 'TestBlue' -Diffuse '#3050FFFF'))
}

#--- a file-backed template ---------------------------------------------------
# The level references this by file, so `list_templates` reports in=file for it
# at load and template_storage has something real to move between the two forms.
Write-Json -File (Join-Path $assets 'Templates\tf_marker.tpl') -Value @{
    name       = 'tf_marker'
    components = @{
        Transform = @{ scale = @{ x = 0.5; y = 0.5; z = 0.5 } }
        Material  = @{ name = 'TestBlue' }
    }
}

#--- the level ----------------------------------------------------------------
$templates = @(
    # A Camera component is what makes EditorCamera find a camera at all; without
    # one every camera_* command answers "no camera (load a level first)".
    @{ name = 'tf_camera'; components = @{
        Camera = @{ position = @{ x = 12.0; y = 9.0; z = -14.0 }
                    direction = @{ x = 0.0; y = 1.0; z = 0.0 } } } },
    # No Mesh/Material block: CreateTemplate substitutes the built-in cube and the
    # default white material, which is what makes an FBX-free fixture possible.
    @{ name = 'tf_box'; components = @{} },
    @{ file = 'Templates\tf_marker.tpl' }
)
$instances = @(
    @{ name = 'camera_rig'; template = 'tf_camera'; position = @{ x = 12.0; y = 9.0; z = -14.0 } },
    @{ name = 'box_a'; template = 'tf_box'; position = @{ x = -3.0; y = 0.0; z = 0.0 } },
    @{ name = 'box_b'; template = 'tf_box'; position = @{ x = 3.0; y = 0.0; z = 0.0 } },
    @{ name = 'box_c'; template = 'tf_box'; position = @{ x = 0.0; y = 0.0; z = 3.0 } }
)
$models = @()
$materialFiles = @('materials\test.mat')

if ($Kind -eq 'models') {
    $trollSource = [IO.Path]::GetFullPath($TrollAssets)
    if (-not (Test-Path $trollSource)) {
        throw "troll assets not found at $trollSource - pass -TrollAssets, or use -Kind empty."
    }
    $trollTarget = Join-Path $assets 'troll'
    New-Item -ItemType Directory -Force $trollTarget | Out-Null
    Copy-Item (Join-Path $trollSource '*') $trollTarget -Recurse -Force

    # troll_tpose carries the mesh, the material and the skeleton; the other two
    # are animation-only files, which is exactly the case that makes a model not
    # a template (create_template_from_model must refuse them).
    $models = @(
        @{ file = 'troll\troll_tpose.fbx'; triangulate = $false },
        @{ file = 'troll\troll_idle.fbx'; triangulate = $false },
        @{ file = 'troll\troll_walk.fbx'; triangulate = $false }
    )
    $materialFiles += 'troll\troll.mat'
    # A ground slab, so the render suite has something for shadows to fall on and
    # something filling the middle of the view to measure.
    $templates += @{ name = 'tf_ground'; components = @{
        Material = @{ name = 'TestRed' }
        Physics  = @{ type = 'STATIC'; shape = 'BOX' }
    } }
    $instances += @{ name = 'ground'; template = 'tf_ground'
                     position = @{ x = 0.0; y = -0.5; z = 0.0 }
                     scale = @{ x = 80.0; y = 1.0; z = 80.0 } }
    $templates += @{ name = 'tf_troll'; components = @{
        Mesh      = @{ name = 'troll'
                       clips = @{ idle = 'troll_idle'; walk = 'troll_walk' }
                       animation = 'idle'; animation_loop = $true; animation_speed = 1.0 }
        Material  = @{ name = 'TrollMaterial' }
        # Scale and the -90 degrees about X that stands a Z-up export upright,
        # matching Tests/DemoGame/Templates/troll.tpl - so world-space assertions
        # ("the collider is taller than it is wide") mean what they say.
        Transform = @{ scale = @{ x = 0.025; y = 0.025; z = 0.025 }
                       rotation = @{ x = -0.70710677; y = 0.0; z = 0.0; w = 0.70710677 } }
        Physics   = @{ type = 'DYNAMIC'; shape = 'CAPSULE' }
    } }
}

Write-Json -File (Join-Path $assets 'Levels\Solo\1\level.json') -Value @{
    world = @{
        path           = $assets + '\'
        models         = $models
        material_files = $materialFiles
        templates      = $templates
        instances      = $instances
        entities       = @()
        # resolution and density are not optional: World::Load reads them off the
        # light with operator[], so a directional light without them is a light
        # that never casts (and the cascade debug view says so on screen).
        lights         = @(
            @{ type = 'ambient'; name = 'ambient'; color_up = '050050050'; color_down = '020020020' },
            @{ type = 'directional'; name = 'sun'; color = '100100100'; cast_shadow = $true
               resolution = 1; density = 15.0
               direction = @{ x = -0.5; y = -1.0; z = -0.3 } }
        )
    }
}

return (Join-Path $assets 'Levels\Solo\1\level.json')
