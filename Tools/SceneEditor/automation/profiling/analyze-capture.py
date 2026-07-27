# Offline analysis of one .rdc capture, run inside qrenderdoc's embedded Python:
#
#     qrenderdoc.exe --python analyze-capture.py
#
# qrenderdoc does NOT forward extra argv, so parameters arrive through the
# HOTBITE_RDC_JOB environment variable pointing at a JSON file:
#     {"cap": "<capture.rdc>", "out": "<result.json>", "log": "...", "done": "..."}
# analyze-captures.ps1 writes that file and polls for the done-marker, because
# qrenderdoc is a GUI process: its stdout is detached and it never exits on its own.
#
# Output is a JSON blob of every draw/dispatch with its GPU duration in ms, plus a
# sha1 of each shader's bytecode. Pass names come from matching those hashes against
# the built *.cso files (see report.py) -- the engine emits no debug markers and every
# shader reports the entry point "main", so reflection alone cannot name a pass.

import os
import json
import traceback

JOB = os.environ.get("HOTBITE_RDC_JOB")
cfg = json.load(open(JOB, encoding="utf-8-sig"))  # utf-8-sig: PowerShell writes a BOM
CAP, OUT = cfg["cap"], cfg["out"]
LOG, DONE = cfg["log"], cfg["done"]

if os.path.exists(DONE):
    os.remove(DONE)

_lines = []


def log(msg):
    _lines.append(str(msg))
    with open(LOG, "w", encoding="utf-8") as f:
        f.write("\n".join(_lines) + "\n")


log("analyzing " + CAP)

import renderdoc as rd


def main():
    cap = rd.OpenCaptureFile()
    if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
        log("ERROR: could not open capture"); return
    if not cap.LocalReplaySupport():
        log("ERROR: no local replay support for this capture"); return
    status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        log("ERROR: replay failed to start: " + str(status)); return
    log("replay opened")

    sdfile = ctrl.GetStructuredFile()

    actions = []

    def walk(items):
        for a in items:
            actions.append(a)
            walk(a.children)

    walk(ctrl.GetRootActions())
    log("actions: %d" % len(actions))

    # ---- per-event GPU duration ----
    durations = {}
    if rd.GPUCounter.EventGPUDuration in ctrl.EnumerateCounters():
        desc = ctrl.DescribeCounter(rd.GPUCounter.EventGPUDuration)
        for r in ctrl.FetchCounters([rd.GPUCounter.EventGPUDuration]):
            v = r.value.d if desc.resultByteWidth == 8 else r.value.f
            if desc.unit == rd.CounterUnit.Seconds:
                v *= 1000.0
            durations[r.eventId] = v
        log("counter results: %d" % len(durations))
    else:
        log("WARNING: EventGPUDuration counter unavailable, timings will be zero")

    # ---- identify shaders by bytecode hash ----
    import hashlib
    shader_bytes = {}

    def shader_sha(pipe, stage):
        try:
            refl = pipe.GetShaderReflection(stage)
            if refl is None:
                return None
            raw = bytes(refl.rawBytes)
            if not raw:
                return None
        except Exception:
            return None
        h = hashlib.sha1(raw).hexdigest()
        shader_bytes[h] = len(raw)
        return h

    # ActionFlags is versioned with RenderDoc and members come and go (MeshDispatch was
    # dropped, and older builds lack it too), so build the mask from what this install
    # actually has instead of naming them all unconditionally - one missing name is an
    # AttributeError that kills the whole replay.
    DRAWISH = 0
    for _flag in ("Drawcall", "Dispatch", "Clear", "Copy", "Resolve", "MeshDispatch"):
        DRAWISH |= int(getattr(rd.ActionFlags, _flag, 0))
    todo = [a for a in actions if a.flags & DRAWISH]
    log("draw/dispatch/clear events: %d" % len(todo))

    events = []
    for i, a in enumerate(todo):
        rec = {
            "eid": a.eventId,
            "name": a.GetName(sdfile),
            "ms": durations.get(a.eventId, 0.0),
            "flags": str(a.flags),
            "numIndices": int(a.numIndices),
            "numInstances": int(a.numInstances),
            "dispatch": [int(x) for x in a.dispatchDimension],
        }
        try:
            ctrl.SetFrameEvent(a.eventId, False)
            pipe = ctrl.GetPipelineState()
            rec["cs"] = shader_sha(pipe, rd.ShaderStage.Compute)
            rec["ps"] = shader_sha(pipe, rd.ShaderStage.Pixel)
            rec["vs"] = shader_sha(pipe, rd.ShaderStage.Vertex)
            try:
                vp = pipe.GetViewport(0)
                rec["viewport"] = [int(vp.width), int(vp.height)]
            except Exception:
                pass
        except Exception as e:
            rec["err"] = repr(e)
        events.append(rec)
        if i % 50 == 0:
            log("  %d/%d" % (i, len(todo)))

    total = sum(e["ms"] for e in events)
    log("total GPU ms: %.3f" % total)

    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"capture": CAP, "total_ms": total,
                   "events": events, "shader_hashes": shader_bytes}, f, indent=1)
    log("wrote " + OUT)

    ctrl.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    log("EXCEPTION\n" + traceback.format_exc())

with open(DONE, "w") as f:
    f.write("done")
