# Aggregates the per-event JSON produced by analyze-capture.py into a per-pass GPU
# timing table, averaged over every capture in the directory.
#
#     python report.py <dir with *.json> <dir with *.cso>
#
# Passes are named by matching each event's shader bytecode sha1 against the built
# .cso files: the engine emits no debug markers and every shader's entry point is
# "main", so this is the only way to tell VolumetricLightCS from TerrainPS.

import sys
import os
import glob
import json
import hashlib
import collections
import statistics

json_dir = sys.argv[1]
cso_dir = sys.argv[2]

cso = {}
for f in glob.glob(os.path.join(cso_dir, "*.cso")):
    with open(f, "rb") as fh:
        cso[hashlib.sha1(fh.read()).hexdigest()] = os.path.basename(f)
if not cso:
    sys.exit("No .cso files in %s -- build that configuration first." % cso_dir)


def pass_name(e):
    """The shader actually driving this event.

    Stage matters: a Dispatch leaves the previous draw's PS/VS still bound, so the
    compute shader is the only meaningful identity for it.
    """
    flags = e["flags"]
    if "Dispatch" in flags:
        h = e.get("cs")
        return cso.get(h, "CS:" + (h[:8] if h else "?"))
    if "Clear" in flags:
        return "<clear>"
    if "Copy" in flags or "Resolve" in flags:
        return "<copy/resolve>"
    h = e.get("ps")
    if h is None:
        return "<depth-only draw>"      # shadow / depth prepass
    return cso.get(h, "PS:" + h[:8])


files = sorted(glob.glob(os.path.join(json_dir, "*.json")))
files = [f for f in files if not f.endswith("analyze-job.json")]
if not files:
    sys.exit("No analysis .json files in %s" % json_dir)

per_cap, totals, calls = {}, {}, collections.Counter()
for path in files:
    with open(path, encoding="utf-8") as fh:
        d = json.load(fh)
    tag = os.path.basename(path).replace(".json", "").replace("sceneeditor_frame", "")
    totals[tag] = d["total_ms"]
    agg = collections.defaultdict(float)
    for e in d["events"]:
        n = pass_name(e)
        agg[n] += e["ms"]
        calls[n] = max(calls[n], sum(1 for x in d["events"] if pass_name(x) == n))
    per_cap[tag] = agg

tags = sorted(per_cap)
mean_total = statistics.mean(totals.values())
print("captures: " + ", ".join("%s (%.2f ms)" % (t, totals[t]) for t in tags))
print("frame total: mean %.2f ms, min %.2f, max %.2f"
      % (mean_total, min(totals.values()), max(totals.values())))
print()

names = set()
for agg in per_cap.values():
    names |= set(agg)

rows = []
for n in names:
    vals = [per_cap[t].get(n, 0.0) for t in tags]
    rows.append((statistics.mean(vals), n, vals))
rows.sort(reverse=True)

hdr = "%-26s %8s %7s %6s  " % ("PASS (shader)", "mean ms", "share", "calls")
hdr += " ".join("%7s" % t for t in tags)
print(hdr)
print("-" * len(hdr))
for mean, n, vals in rows:
    if mean < 0.02:
        continue
    print("%-26s %8.3f %6.1f%% %6d  " % (n[:26], mean, 100 * mean / mean_total, calls[n])
          + " ".join("%7.3f" % v for v in vals))

print()
print("Timings are replay-time GPU durations summed per event: reliable for RANKING")
print("passes, not an absolute frame budget (work that overlaps at runtime is counted")
print("serially here). Compare the per-capture columns -- a pass whose columns disagree")
print("2-4x is scene/view dependent, so optimise against several captures, not one.")
