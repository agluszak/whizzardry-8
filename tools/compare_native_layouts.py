#!/usr/bin/env python3
"""Compare protected raw-IO record extents/field offsets between Clang lanes.

Consumes audit_native_layouts.py inventories. Runtime-only pointer tails may
grow beyond an explicitly recorded wire prefix. Missing records, failed units
and incompatible layouts cause failure. This verifies the protected list, not
that every serialized type or pointer-use path has already been identified.
"""

import argparse
import collections
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def layouts(inventory, name, prefix):
    records = [
        f for f in inventory["facts"] if f["kind"] == "record" and f["name"] == name
    ]
    shapes = []
    for record in records:
        fields = [
            (f["name"], f["offset_bits"], f["size"])
            for f in record["fields"]
            if prefix is None or 0 <= f["offset_bits"] < prefix * 8
        ]
        shape = {"size": record["size"] if prefix is None else prefix, "fields": fields}
        if shape not in shapes:
            shapes.append(shape)
    return shapes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("windows", type=Path)
    args = parser.parse_args()
    native = json.loads(args.native.read_text())
    windows = json.loads(args.windows.read_text())
    rules = json.loads((ROOT / "tools/native_codemod_rules.json").read_text())
    rows = []
    for name in sorted(rules["raw_io_records"]):
        prefix = rules.get("raw_io_prefixes", {}).get(name)
        n = layouts(native, name, prefix)
        w = layouts(windows, name, prefix)
        # Multiple compiled variants must agree; do not silently select one.
        status = "equal" if len(n) == len(w) == 1 and n == w else "different"
        if not n or not w:
            status = "missing"
        rows.append(
            {
                "name": name,
                "wire_prefix": prefix,
                "status": status,
                "native": n,
                "windows": w,
            }
        )
    errors = [
        {"lane": lane, **unit}
        for lane, inv in [("native", native), ("windows", windows)]
        for unit in inv["units"]
        if unit["errors"]
    ]
    print(
        json.dumps(
            {
                "counts": dict(collections.Counter(r["status"] for r in rows)),
                "failed_units": errors,
                "records": rows,
            },
            indent=2,
        )
    )
    return int(bool(errors) or any(r["status"] != "equal" for r in rows))


if __name__ == "__main__":
    raise SystemExit(main())
