# GPU frame profiling

Capture frames of a level from the Scene Editor and get a per-pass GPU timing table,
with no mouse input and no RenderDoc UI. Built on the automation channel
(`../README.md`) plus RenderDoc's in-application API.

## Prerequisites

- RenderDoc installed at `C:\Program Files\RenderDoc` (`winget install RenderDoc.RenderDoc`).
- A **Release** x64 build of SceneEditor and the engine's shaders in
  `Solution/x64/Release`. Profile Release, never Debug -- Debug timings are meaningless.
- `python` on PATH (only for the final aggregation; the replay uses qrenderdoc's own
  embedded interpreter).

## The loop

```powershell
$dir = 'C:\tmp\prof'

# 1. capture 4 frames of the demo scene from a representative viewpoint
Tools\SceneEditor\automation\profiling\capture-frames.ps1 -OutDir $dir -Count 4 `
    -CameraPos '-54 16 -54' -CameraTarget '-60 12.6 -61'

# 2. replay them and print the pass table (minutes per capture -- see below)
Tools\SceneEditor\automation\profiling\analyze-captures.ps1 -Dir $dir
```

`capture-frames.ps1` launches the editor with `--renderdoc`, waits for the scene to
settle, places the camera, triggers `rdoc_capture` N times and quits.
`analyze-captures.ps1` replays each `.rdc` into a `.json` of per-event GPU durations and
runs `report.py` to aggregate. Re-print an existing report without replaying with
`-ReportOnly`.

Output looks like:

```
captures: 3949 (13.13 ms), 4012 (14.55 ms), 4149 (11.50 ms), 4287 (12.23 ms)
frame total: mean 12.85 ms, min 11.50, max 14.55

PASS (shader)               mean ms   share  calls     3949    4012    4149    4287
TerrainPS.cso                 2.878   22.4%     18    3.126   3.201   2.543   2.642
<depth-only draw>             1.860   14.5%    113    1.975   1.981   1.719   1.763
LavaPS.cso                    1.215    9.4%      2    1.145   1.435   1.134   1.145
GIAverageCS.cso               1.032    8.0%      3    1.006   1.282   0.806   1.035
VolumetricLightCS.cso         0.982    7.6%      1    0.841   1.245   0.894   0.949
...
```

(demo scene, 1536x793, RT high + AA + motion blur + lens flare, RTX 4050 Laptop.)

## Read the numbers correctly

**Close the editor before replaying.** This is the biggest trap. A running SceneEditor
competes with the replay for the GPU and inflates the results *unevenly*, which reorders
the ranking and will send you optimising the wrong pass. Measured on the same four
captures, editor running vs closed:

| | contaminated | clean |
|---|---|---|
| frame total (capture 4287) | 20.67 ms | 12.23 ms |
| VolumetricLightCS | 3.93 ms — **1st**, 20.5% | 0.98 ms — 5th, 7.6% |
| TerrainPS | 2.78 ms — 2nd | 2.88 ms — **1st**, 22.4% |

Note the contention did not scale everything equally: TerrainPS barely moved while the
volumetric pass quadrupled, so you cannot correct for it after the fact.
`capture-frames.ps1` quits the editor for you unless you pass `-KeepOpen`; if you did,
close it before analyzing. Same goes for anything else heavy on the GPU.

**Take several captures, never one.** The demo scene animates (physics, emissive lava).
On a clean run the passes are tight (a few percent apart across captures), so a pass whose
columns disagree by 2x is either genuinely view-dependent or a sign that something else
was using the GPU -- check the frame totals agree before trusting a ranking.

**These are replay-time durations summed per event.** They rank passes reliably; they are
not an absolute frame budget, because work that overlaps on the real timeline is counted
serially here. The frame total will not match the editor's on-screen frame time (which is
vsync-locked at 60 fps anyway).

## How passes get their names

The engine emits **no** debug markers (no `ID3DUserDefinedAnnotation`), and every shader
reports the entry point `"main"` with no debug info, so RenderDoc cannot name anything --
the frame is a flat list of ~260 `DrawIndexed`/`Dispatch` calls.

`report.py` recovers real names by sha1-hashing each event's shader bytecode and matching
it against the built `*.cso` files in `Solution/x64/<Config>`. That resolves ~31 of 33
shaders. Consequences:

- Analyze with the **same configuration you captured** (`-Config`), or the hashes miss.
- If you rebuild shaders between capturing and analyzing, names degrade to `PS:<hash>`.
- Unmatched events show as `CS:`/`PS:<first 8 hex>`; `<depth-only draw>` means no pixel
  shader was bound (shadow/depth prepass).

Stage selection matters: a `Dispatch` leaves the previous draw's PS/VS bound, so compute
events are identified by their CS only.

## Why the scripts look the way they do

Three qrenderdoc behaviours dictate the plumbing; don't "simplify" them away:

- `qrenderdoc --python <script>` does **not** forward extra argv. Parameters travel in
  the `HOTBITE_RDC_JOB` environment variable, pointing at a JSON job file.
- qrenderdoc is a GUI process: **stdout is detached** (the script logs to a file) and it
  **never exits on its own** (the driver polls a done-marker, then kills it). Waiting on
  process exit always times out.
- PowerShell's `Out-File`/`>` write a UTF-8 BOM that breaks `json.load`, hence
  `[IO.File]::WriteAllText` on the writing side and `encoding="utf-8-sig"` on the reading
  side.

Replay is slow (minutes for a ~1 GB capture): `FetchCounters` replays the frame
repeatedly, and every event needs a `SetFrameEvent` to read back its pipeline state.
Captures are ~1 GB each, so mind the disk and clear the directory between sessions
(`capture-frames.ps1` does that for you).

## Profiling something other than the demo scene

Pass `-Level <path>` to `capture-frames.ps1`. Pick the camera with the `camera_pos` /
`camera_target` automation commands first (see `../README.md`) and check the framing with
`screenshot` -- the demo scene's saved camera, for instance, starts buried inside the lava
surface, which profiles nothing useful. Toggle features with the `render` command to
A/B a pass: `render rt_indirect 0`, `render aa 0`, `render rt_quality off`.
