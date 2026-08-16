# fixture: empty
# description: Shader hot reload - the source index, recompiling into the running editor, and what a broken shader does (nothing).

# Everything here goes through a *copy* of an engine shader in a scratch folder
# registered with `shader_sources add`, never the tree itself: a folder added later
# wins the name collision, so the copy is what compiles, and removing the folder
# puts the original back. A suite that edited Engine\...\Shaders would leave the
# repo modified when it failed halfway.
#
# The folder must sit *outside* the project: the editor registers the open
# project's own folder as a source folder (a game keeps its shaders there - the
# demo's Terrain* are exactly that), so a scratch copy under $Project is found
# again by the project scan the moment the explicit entry is removed, and the
# "reverting restores it" half of the test can never pass.
$ScratchSources = Join-Path (Split-Path $ShotDir -Parent) 'shader-scratch'
New-Item -ItemType Directory -Force $ScratchSources | Out-Null

# PostMainPS is the whole test target: the last pass of the post-process chain, so
# what it returns is what the viewport shows, and it is small enough to compile in
# milliseconds (GIRayTraceCS takes 36 seconds - see ShaderReload.h).
$PostMainSource = Join-Path $PSScriptRoot '..\..\..\..\..\Engine\Engine\Core\Shaders\PostProcess\PostMainPS.hlsl'
$PostMainSource = (Resolve-Path $PostMainSource).Path

function Write-ScratchShader {
    param([string]$Name, [string]$Text)
    # No BOM: fxc rejects one in an included file, and it is the same trap the
    # command channel has with Set-Content -Encoding utf8.
    $path = Join-Path $ScratchSources $Name
    [IO.File]::WriteAllText($path, $Text, (New-Object Text.UTF8Encoding($false)))
    return $path
}

# A reload answers when the work is *queued*; the compile runs on a worker thread.
function Wait-ShaderReload {
    param([int]$TimeoutSec = 120)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $r = (SendOk 'shader_reload_status')[0]
        if ($r.Text -match '^idle') { return $r }
        Start-Sleep -Milliseconds 200
    }
    throw "shader reload did not finish within ${TimeoutSec}s"
}

Test 'the shader source index finds the engine tree' {
    $r = (SendOk 'shader_sources')[0]
    Assert-Match -Pattern '^\d+ shader sources' -Actual $r.Text
    $count = [int]($r.Text -replace ' .*$', '')
    Assert-True -Condition ($count -gt 20) -Message "indexed $count sources"
    Assert-True -Condition (($r.Payload -join "`n") -match 'Core\\Shaders') `
        -Message 'the engine shader folder is registered'
}

Test 'every loaded shader resolves to a source' {
    $r = (SendOk 'shaders_loaded')[0]
    Assert-Match -Pattern '^\d+ shaders loaded' -Actual $r.Text
    $without = @($r.Payload | Where-Object { $_ -match '<no source>' })
    Assert-Equal -Expected 0 -Actual $without.Count -Message "shaders with no source: $($without -join ', ')"
    Assert-True -Condition (($r.Payload -join "`n") -match 'PostMainPS\.cso=') -Message 'PostMainPS is loaded'
}

Test 'a shader recompiles into the running editor' {
    SendOk 'reload_shaders PostMainPS.cso' | Out-Null
    $status = Wait-ShaderReload
    Assert-Match -Pattern 'errors=0' -Actual $status.Text
    Assert-Match -Pattern '1 reloaded, 0 failed' -Actual $status.Text
}

Test 'the bare stem names the same shader' {
    SendOk 'reload_shaders PostMainPS' | Out-Null
    $status = Wait-ShaderReload
    Assert-Match -Pattern 'errors=0' -Actual $status.Text
}

Test 'an unknown shader is rejected without queueing anything' {
    Assert-Err -Result (Send 'reload_shaders NoSuchShader.cso')[0] -Pattern 'not loaded'
    Assert-Equal -Expected 0 -Actual ([int]((SendOk 'shader_reload_status')[0].Text -replace '.*pending=(\d+).*', '$1'))
}

Test 'nothing has changed, so a changed-only reload queues nothing' {
    $r = (SendOk 'reload_shaders changed')[0]
    Assert-Match -Pattern 'queued 0 shader' -Actual $r.Text
}

Test 'an edited source changes what the viewport shows, and reverting restores it' {
    Step-EditorFrames -Session $Session -Count 3
    $before = Shot 'shader-before'

    # The same shader, returning a flat colour instead of the scene.
    Write-ScratchShader -Name 'PostMainPS.hlsl' -Text @'
Texture2D renderTexture : register(t0);
SamplerState basicSampler : register(s0);

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
'@ | Out-Null

    SendOk "shader_sources add ""$ScratchSources""" | Out-Null
    SendOk 'reload_shaders PostMainPS.cso' | Out-Null
    Wait-ShaderReload | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $after = Shot 'shader-after'

    $diff = Get-ImageDifference -PathA $before -PathB $after
    Assert-True -Condition ($diff.DifferingShare -gt 0.5) `
        -Message "the reloaded shader reached the frame (differing share $($diff.DifferingShare))"
    $stats = Get-ImageStats -Path $after
    Assert-True -Condition ($stats.MeanR -gt 200 -and $stats.MeanG -lt 60) `
        -Message "the viewport is the colour the edited shader returns (R $($stats.MeanR), G $($stats.MeanG))"

    # Dropping the scratch folder uncovers the original source again.
    SendOk "shader_sources remove ""$ScratchSources""" | Out-Null
    SendOk 'reload_shaders PostMainPS.cso' | Out-Null
    Wait-ShaderReload | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $restored = Shot 'shader-restored'
    $back = Get-ImageDifference -PathA $before -PathB $restored
    Assert-True -Condition ($back.DifferingShare -lt 0.2) `
        -Message "the original shader is back (differing share $($back.DifferingShare))"
}

Test 'a shader that does not compile keeps the running one and reports the error' {
    Step-EditorFrames -Session $Session -Count 3
    $before = Shot 'broken-before'

    Write-ScratchShader -Name 'PostMainPS.hlsl' -Text @'
float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    this is not HLSL
}
'@ | Out-Null

    SendOk "shader_sources add ""$ScratchSources""" | Out-Null
    SendOk 'reload_shaders PostMainPS.cso' | Out-Null
    $status = Wait-ShaderReload
    Assert-Match -Pattern '0 reloaded, 1 failed' -Actual $status.Text
    Assert-True -Condition (($status.Payload -join ' ') -match 'error X') `
        -Message "the compiler's message came back: $($status.Payload -join ' ')"

    # The point of compiling before swapping: the frame is untouched.
    Step-EditorFrames -Session $Session -Count 3
    $after = Shot 'broken-after'
    $diff = Get-ImageDifference -PathA $before -PathB $after
    Assert-True -Condition ($diff.DifferingShare -lt 0.2) `
        -Message "a failed compile left the frame alone (differing share $($diff.DifferingShare))"

    SendOk "shader_sources remove ""$ScratchSources""" | Out-Null
}

# A shader body that takes its colour from an included header, so an edit to the
# *header* is what the change detector has to notice. This is the case the build
# itself gets wrong (FxCompile does not track .hlsli includes here - see
# CLAUDE.md), and the reloader gets it right because the list comes from the
# compiler: it is the files fxc actually opened.
$TintedShader = @'
#include "TestTint.hlsli"

Texture2D renderTexture : register(t0);
SamplerState basicSampler : register(s0);

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    return TEST_TINT;
}
'@

Test 'editing an included header marks the shader that includes it as changed' {
    Write-ScratchShader -Name 'TestTint.hlsli' -Text "#define TEST_TINT float4(1.0f, 0.0f, 0.0f, 1.0f)`n" | Out-Null
    Write-ScratchShader -Name 'PostMainPS.hlsl' -Text $TintedShader | Out-Null
    SendOk "shader_sources add ""$ScratchSources""" | Out-Null
    SendOk 'reload_shaders PostMainPS.cso' | Out-Null
    Wait-ShaderReload | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    Assert-True -Condition ((Get-ImageStats -Path (Shot 'tint-red')).MeanR -gt 200) -Message 'the tinted shader is running'

    # Nothing has moved since that compile.
    Assert-Match -Pattern 'queued 0 shader' -Actual (SendOk 'reload_shaders changed')[0].Text

    # Only the header changes. Nothing else in the tree does, so exactly one
    # shader may be queued - the one whose include set names this file.
    Write-ScratchShader -Name 'TestTint.hlsli' -Text "#define TEST_TINT float4(0.0f, 1.0f, 0.0f, 1.0f)`n" | Out-Null
    Assert-Match -Pattern 'queued 1 shader' -Actual (SendOk 'reload_shaders changed')[0].Text
    Wait-ShaderReload | Out-Null
    Step-EditorFrames -Session $Session -Count 3
    $stats = Get-ImageStats -Path (Shot 'tint-green')
    Assert-True -Condition ($stats.MeanG -gt 200 -and $stats.MeanR -lt 60) `
        -Message "the header edit reached the frame (R $($stats.MeanR), G $($stats.MeanG))"
}

Test 'auto reload picks up an edit without being asked' {
    SendOk 'menu "Shaders/Auto Reload on Change"' | Out-Null
    try {
        Write-ScratchShader -Name 'TestTint.hlsli' -Text "#define TEST_TINT float4(0.0f, 0.0f, 1.0f, 1.0f)`n" | Out-Null
        # The watcher polls every 700 ms and the compile follows on the worker;
        # this waits for the result rather than for a fixed number of frames.
        $deadline = (Get-Date).AddSeconds(30)
        $blue = $false
        while (-not $blue -and (Get-Date) -lt $deadline) {
            Step-EditorFrames -Session $Session -Count 5
            $blue = (Get-ImageStats -Path (Shot 'auto-tint')).MeanB -gt 200
        }
        Assert-True -Condition $blue -Message 'the watcher reloaded the edited shader on its own'
    }
    finally {
        SendOk 'menu "Shaders/Auto Reload on Change"' | Out-Null
        SendOk "shader_sources remove ""$ScratchSources""" | Out-Null
        SendOk 'reload_shaders PostMainPS.cso' | Out-Null
        Wait-ShaderReload | Out-Null
    }
}

Test 'the Shaders menu drives the same reload' {
    $menus = (SendOk 'menus')[0].Payload -join "`n"
    foreach ($item in @('Shaders/Reload Changed', 'Shaders/Reload All', 'Shaders/Auto Reload on Change')) {
        Assert-True -Condition ($menus -match [regex]::Escape($item)) -Message "menu $item is registered"
    }
    SendOk 'menu "Shaders/Reload Changed"' | Out-Null
    Wait-ShaderReload | Out-Null
}
