"""Compiler-based first-party translation unit and include discovery."""

from pathlib import Path
import shlex
import subprocess

SOURCES = {".c", ".cc", ".cpp", ".cxx"}


def source_path(entry):
    return (Path(entry["directory"]) / entry["file"]).resolve()


def is_first_party_source(root, path):
    try:
        relative = path.relative_to(root)
    except ValueError:
        return False
    return relative.parts[0] in ("src", "tests", "tools") and path.suffix in SOURCES


def dependencies(entry, index, temp):
    directory = Path(entry["directory"])
    arguments = entry.get("arguments")
    if arguments is None:
        arguments = shlex.split(entry["command"])
    depfile = temp / f"{index}.d"
    # The compiler handles conditional/macro includes, -include, search paths,
    # and transitive includes. -M runs only the preprocessor, not compilation.
    # -c is unused in preprocessing mode; with -Werror it makes Clang's
    # dependency scan fail. Do not let a scan write a compilation output.
    command = []
    skip_output = False
    for argument in arguments:
        if skip_output:
            skip_output = False
        elif argument == "-o":
            skip_output = True
        elif argument != "-c":
            command.append(argument)
    cmd = [*command, "-M", "-MT", "deps", "-MF", str(depfile)]
    completed = subprocess.run(cmd, cwd=directory, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, timeout=90, check=False)
    if completed.returncode:
        message = completed.stderr.decode(errors="replace")[-2000:]
        raise RuntimeError(f"{entry['file']}: {message}")
    output = depfile.read_text().replace("\\\n", "")
    if not output.startswith("deps:"):
        raise RuntimeError(f"Unexpected dependency output for {entry['file']}")
    names = shlex.split(output[len("deps:"):])
    return source_path(entry), {(directory / name).resolve() for name in names}
