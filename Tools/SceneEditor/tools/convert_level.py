#!/usr/bin/env python3
"""Convert a HotBite level.json from the flat-key format to the component format.

Before, component data was spread across ad-hoc sibling keys of an entity record,
with a different spelling per component and no way to say "remove this one":

    { "name": "Cube*", "material": "floor", "cast_shadow": false,
      "position": {...}, "physics": { "type": "KINEMATIC" } }

After, every component is a block under "components", keyed by the component's
registered NAME, plus an optional "remove" list:

    { "name": "Cube*",
      "components": {
        "Base":      { "cast_shadow": false },
        "Transform": { "position": {...} },
        "Material":  { "name": "floor" },
        "Physics":   { "type": "KINEMATIC" }
      } }

That uniformity is what lets World::Load drive components it has no compile-time
knowledge of, and what gives per-entity overrides somewhere to live.

Usage:
    python convert_level.py LEVEL.json [LEVEL.json ...]      # convert in place
    python convert_level.py --dry-run LEVEL.json             # print, don't write

Safe to re-run: a record that already has a "components" block is left alone.
"""

import argparse
import json
import sys
from collections import OrderedDict

# Old key -> (component name, new key). Keys whose meaning is unchanged inside the
# component block just move; the ones needing more than a move are handled below.
SIMPLE_MOVES = {
    "cast_shadow":     ("Base", "cast_shadow"),
    "pass":            ("Base", "pass"),
    "parent":          ("Base", "parent"),
    "parent_position": ("Base", "parent_position"),
    "parent_rotation": ("Base", "parent_rotation"),
    "visible":         ("Base", "visible"),
    "scene_visible":   ("Base", "scene_visible"),
    "draw_depth":      ("Base", "draw_depth"),
    "is_static":       ("Base", "is_static"),
    "position":        ("Transform", "position"),
    "rotation":        ("Transform", "rotation"),
    "scale":           ("Transform", "scale"),
}

# Keys that stay at the top level of the record because they are not component
# data: they identify the record or drive how the entity is created in the first
# place (SpawnInstance's arguments, CloneEntity's source).
TOP_LEVEL_KEEP = {
    "entities":  {"name", "rename"},
    "instances": {"name", "template", "position", "rotation", "scale", "material"},
    "clones":    {"name", "source", "position", "rotation", "scale"},
}


def convert_record(record, section):
    """Return a new record with flat component keys folded into "components"."""
    if not isinstance(record, dict):
        return record
    if "components" in record:
        return record  # already converted

    keep = TOP_LEVEL_KEEP[section]
    out = OrderedDict()
    components = OrderedDict()

    def block(name):
        return components.setdefault(name, OrderedDict())

    for key, value in record.items():
        if key in keep:
            out[key] = value
            continue

        if key in SIMPLE_MOVES:
            component, new_key = SIMPLE_MOVES[key]
            block(component)[new_key] = value
        elif key == "physics":
            # Already the right shape (type/shape/bounce/friction/air_friction).
            block("Physics").update(value)
        elif key == "player":
            # A tag component: presence is the payload, and "player": false meant
            # "no component at all" rather than a component holding false.
            if value:
                block("Player")
        elif key == "material":
            block("Material")["name"] = value
        elif key == "multi_texture":
            block("Material")["multi_texture"] = value
        elif key == "template":
            # The old key adopted a template entity's mesh *and* material in one go;
            # the two are now separable, so both blocks get it to preserve behaviour.
            block("Mesh")["template"] = value
            block("Material")["template"] = value
        else:
            # Anything unrecognized is left where it is rather than guessed at, so a
            # hand-authored key survives the conversion visibly instead of vanishing.
            print("  note: keeping unrecognized key %r at top level" % key,
                  file=sys.stderr)
            out[key] = value

    if components:
        out["components"] = components
    return out


def convert_level(level):
    world = level.get("world")
    if not isinstance(world, dict):
        return level, 0
    changed = 0
    for section in ("entities", "instances", "clones"):
        records = world.get(section)
        if not isinstance(records, list):
            continue
        converted = []
        for record in records:
            new_record = convert_record(record, section)
            if new_record is not record:
                changed += 1
            converted.append(new_record)
        world[section] = converted
    return level, changed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", help="level.json files to convert")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the result instead of writing it back")
    args = parser.parse_args()

    for path in args.files:
        with open(path, "r", encoding="utf-8") as handle:
            level = json.load(handle, object_pairs_hook=OrderedDict)

        level, changed = convert_level(level)
        text = json.dumps(level, indent=4)

        if args.dry_run:
            print("=== %s (%d records) ===" % (path, changed))
            print(text)
        else:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
                handle.write("\n")
            print("%s: converted %d record(s)" % (path, changed))


if __name__ == "__main__":
    main()
