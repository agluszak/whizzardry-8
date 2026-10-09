#!/usr/bin/env python3
"""Collect compiler-backed layout and raw-IO review facts from a compilation DB.

Requires the Python bindings shipped with LLVM, not an additional pip package.
Use --llvm with the installed LLVM prefix. Run separately for the native and
Windows databases; rejected translation units remain explicit in the output.
Packed member addresses/arrays are review leads, not proven unsafe accesses.
Only compiled branches have type evidence; audit_allocations.py covers tracked
source text, including inactive branches and modules outside these databases.
"""

import argparse
import concurrent.futures
import functools
import json
import shlex
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IO_NAMES = {"FileRead", "FileWrite", "W8ReadFile", "W8WriteFile", "fread", "fwrite"}


def compiler_args(entry):
    argv = entry.get("arguments") or shlex.split(entry["command"])
    source = Path(entry["directory"], entry["file"]).resolve()
    args = ["--driver-mode=cl"] if "clang-cl" in Path(argv[0]).name else []
    skip = False
    for arg in argv[1:]:
        if skip:
            skip = False
            continue
        if arg in {"-o", "-MF", "-MT", "-MQ"}:
            skip = True
            continue
        if arg in {"-c", "--", "-MD", "-MMD"} or arg.startswith(
            ("/Fo", "/Fd", "-clang:-M")
        ):
            continue
        if Path(entry["directory"], arg).resolve() == source:
            continue
        args.append(arg)
    args.append("-working-directory=" + entry["directory"])
    return str(source), args


def inspect(entry):
    from clang import cindex

    source, args = compiler_args(entry)
    args.append("-resource-dir=" + entry["resource_dir"])
    index = cindex.Index.create()
    tu = index.parse(source, args=args)
    errors = [str(d) for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    result = {"unit": str(Path(source).relative_to(ROOT)), "errors": errors}
    if errors:
        return result
    facts = []

    @functools.cache
    def source_path(filename):
        try:
            path = Path(filename).resolve().relative_to(ROOT)
        except ValueError:
            return None
        return str(path) if path.parts[0] in {"src", "include", "tests"} else None

    def location(cursor):
        if not cursor.location.file:
            return None
        path = source_path(str(cursor.location.file))
        if path is None:
            return None
        return {
            "path": path,
            "line": cursor.location.line,
            "column": cursor.location.column,
        }

    def type_info(ctype):
        canonical = ctype.get_canonical()
        return {
            "type": ctype.spelling,
            "canonical": canonical.spelling,
            "size": ctype.get_size(),
            "align": ctype.get_align(),
        }

    def text(cursor):
        return " ".join(t.spelling for t in cursor.get_tokens())

    def visit(cursor, ancestors=()):
        loc = location(cursor)
        if cursor.location.file and loc is None:
            return
        kind = cursor.kind
        if (
            loc
            and kind
            in {
                cindex.CursorKind.STRUCT_DECL,
                cindex.CursorKind.CLASS_DECL,
                cindex.CursorKind.UNION_DECL,
                cindex.CursorKind.ENUM_DECL,
            }
            and cursor.is_definition()
            and cursor.type.get_size() >= 0
        ):
            fields = []
            for field in cursor.get_children():
                if field.kind == cindex.CursorKind.FIELD_DECL:
                    fields.append(
                        {
                            "name": field.spelling,
                            "offset_bits": field.get_field_offsetof(),
                            **type_info(field.type),
                        }
                    )
            facts.append(
                {
                    "kind": "record",
                    **loc,
                    "name": cursor.spelling,
                    "usr": cursor.get_usr(),
                    **type_info(cursor.type),
                    "fields": fields,
                }
            )
        if loc and kind == cindex.CursorKind.CALL_EXPR:
            name = cursor.spelling
            referenced = cursor.referenced
            owner = referenced.semantic_parent.spelling if referenced else ""
            if name in IO_NAMES | {"qsort", "bsearch"} or (
                name in {"Read", "Write"} and owner == "W8Chunk"
            ):
                arguments = []
                for arg in cursor.get_arguments():
                    # Keep implicit casts and their source types visible: a void*
                    # parameter alone hides the actual record being transferred.
                    types = []

                    def collect(node, types=types):
                        if node.type.kind != cindex.TypeKind.INVALID:
                            info = type_info(node.type)
                            if info not in types:
                                types.append(info)
                        for child in node.get_children():
                            collect(child, types)

                    collect(arg)
                    arguments.append({"expression": text(arg), "types": types})
                facts.append(
                    {
                        "kind": "call",
                        **loc,
                        "name": name,
                        "owner": owner,
                        "arguments": arguments,
                    }
                )
        if loc and kind == cindex.CursorKind.MEMBER_REF_EXPR:
            field = cursor.referenced
            if field and field.kind == cindex.CursorKind.FIELD_DECL:
                record = field.semantic_parent
                align = record.type.get_align()
                field_align = field.type.get_align()
                address = any(
                    p.kind == cindex.CursorKind.UNARY_OPERATOR
                    and next(p.get_tokens(), None).spelling == "&"
                    for p in ancestors[-3:]
                    if next(p.get_tokens(), None)
                )
                array = field.type.get_canonical().kind in {
                    cindex.TypeKind.CONSTANTARRAY,
                    cindex.TypeKind.INCOMPLETEARRAY,
                }
                if align > 0 and field_align > align and (address or array):
                    facts.append(
                        {
                            "kind": "packed_member_candidate",
                            **loc,
                            "expression": text(cursor),
                            "record": record.spelling,
                            "record_align": align,
                            "field": field.spelling,
                            "offset_bits": field.get_field_offsetof(),
                            "address_taken": address,
                            **type_info(field.type),
                        }
                    )
        for child in cursor.get_children():
            visit(child, (*ancestors[-3:], cursor))

    visit(tu.cursor)
    result["facts"] = facts
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument(
        "--llvm", required=True, type=Path, help="installed LLVM prefix"
    )
    parser.add_argument(
        "--unit", action="append", default=[], help="source path substring"
    )
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    bindings = sorted(args.llvm.glob("lib/python*/site-packages/clang/cindex.py"))
    if not bindings:
        parser.error("LLVM Python bindings not found beneath --llvm")
    sys.path.insert(0, str(bindings[-1].parents[1]))
    from clang import cindex

    cindex.Config.set_library_path(str(args.llvm / "lib"))
    entries = json.loads(args.database.read_text())
    entries = [
        e
        for e in entries
        if Path(e["file"]).suffix in {".c", ".cpp", ".cc"}
        and (not args.unit or any(u in e["file"] for u in args.unit))
    ]
    resources = sorted(args.llvm.glob("lib/clang/*/include/stddef.h"))
    if not resources:
        parser.error("Clang resource headers not found beneath --llvm")
    for entry in entries:
        entry["resource_dir"] = str(resources[-1].parents[1])
    # MSVC-driver parsing concurrently stalled inside libclang on this host;
    # the same units finish promptly with serial indexes. Native units retain
    # parallel parsing. Each translation unit owns its own index.
    msvc = any(
        "clang-cl" in Path((e.get("arguments") or shlex.split(e["command"]))[0]).name
        for e in entries
    )
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=1 if msvc else args.jobs
    ) as pool:
        units = list(pool.map(inspect, entries))
    unique = {}
    for unit in units:
        for fact in unit.pop("facts", []):
            unique[json.dumps(fact, sort_keys=True)] = fact
    facts = sorted(unique.values(), key=lambda f: (f["path"], f["line"], f["kind"]))
    print(json.dumps({"units": units, "facts": facts}, indent=2))
    return int(any(u["errors"] for u in units))


if __name__ == "__main__":
    sys.exit(main())
