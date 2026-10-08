#!/usr/bin/env python3
"""Rerunnable source rewrites that make the recovered code compile natively.

Every pass keeps the 32-bit Windows build's code unchanged and is idempotent,
so it can be applied again to files ported from the decomp repository.
Passes driven by diagnostics read build-native/probe.jsonl from
tools/native_probe.py.

Passes:
  includes     Add the compat include to files that use Win32 data types or
               SGP types without including a header that declares them.
  wide-params  Spell UINT16* string parameters as CHAR16* where callers pass
               wchar_t strings (identical on VC6, distinct natively).
  wide-vars    The caller side: UINT16 string buffers handed to CHAR16*
               parameters are declared CHAR16.
  rename       Token-level renames from tools/native_codemod_rules.json:
               Win32 calls become the portable wrappers of compat/platform.h.
  abi-asserts  static_assert -> W8_ABI_ASSERT for layout checks that fail
               natively (in-memory objects; raw I/O records are fixed instead).

Usage: native_codemod.py PASS [--probe FILE] [--dry-run] [FILE...]
"""
import argparse
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = os.path.join(ROOT, 'tools', 'native_codemod_rules.json')

TOKEN = re.compile(
    r'//[^\n]*'
    r'|/\*.*?\*/'
    r'|"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r'|[A-Za-z_][A-Za-z_0-9]*'
    r'|\s+'
    r'|.',
    re.S)


def read(path):
    with open(os.path.join(ROOT, path), encoding='latin-1', newline='') as handle:
        return handle.read()


def write(path, text, dry_run):
    if dry_run:
        print(f'would rewrite {path}')
        return
    with open(os.path.join(ROOT, path), 'w', encoding='latin-1', newline='') as handle:
        handle.write(text)


def load_probe(path):
    entries = []
    with open(path) as handle:
        for line in handle:
            entries.append(json.loads(line))
    return entries


def relative(path):
    return os.path.relpath(os.path.join(ROOT, path), ROOT)


def is_project_file(path):
    """Recovered sources the passes may edit: not vendor headers, not platform units."""
    rules = load_rules()
    excluded = set(rules.get('vendor_headers', [])) | set(rules['platform_units'])
    return path.startswith(('src/', 'include/')) and path not in excluded


def load_rules():
    with open(RULES) as handle:
        return json.load(handle)


# --- includes -------------------------------------------------------------

def is_guarded(lines):
    """True when the file opens with an #ifndef include guard."""
    for current in lines[:40]:
        if re.match(r'\s*#\s*ifndef\s+\w+', current):
            return True
        if re.match(r'\s*#\s*(include|if|pragma once)', current):
            return False
    return False


def add_include(text, include):
    line = f'#include "{include}"'
    if line in text:
        return text
    lines = text.split('\n')
    last = -1
    depth = 0
    for index, current in enumerate(lines[:200]):
        if re.match(r'\s*#\s*if', current):
            depth += 1
        elif re.match(r'\s*#\s*endif', current):
            depth -= 1
        elif depth <= (1 if is_guarded(lines) else 0) and re.match(r'\s*#\s*include\b', current):
            last = index
    if last >= 0:
        lines.insert(last + 1, line)
    else:
        guard = next((i for i, current in enumerate(lines[:50])
                      if re.match(r'\s*#\s*(pragma once|define\s+\w+\s*$)', current)), -1)
        lines.insert(guard + 1, line)
    return '\n'.join(lines)


def pass_include_edits(files, dry_run):
    """Table-driven removals and replacements of include lines."""
    rules = load_rules()
    edits = collections.defaultdict(dict)
    for path, names in rules.get('drop_includes', {}).items():
        for name in names:
            edits[path][name] = None
    for path, table in rules.get('replace_includes', {}).items():
        edits[path].update(table)
    for path, table in sorted(edits.items()):
        if files and path not in files:
            continue
        text = read(path)
        result = text
        for name, replacement in table.items():
            line = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"' + re.escape(name) + r'"[^\n]*\n', re.M)
            result = line.sub('' if replacement is None else replacement + '\n', result)
        if result != text:
            write(path, result, dry_run)
            print(f'{path}: include edits')


def pass_includes(entries, files, dry_run):
    pass_include_edits(files, dry_run)
    table = load_rules()['includes']
    wanted = collections.defaultdict(set)
    for entry in entries:
        if entry['kind'] != 'error':
            continue
        match = re.match(r"(?:unknown type name|use of undeclared identifier) '(\w+)'", entry['message'])
        if not match or match.group(1) not in table:
            continue
        path = relative(entry['file'])
        if is_project_file(path) and (not files or path in files):
            wanted[path].add(table[match.group(1)])
    for path, includes in sorted(wanted.items()):
        text = read(path)
        result = text
        for include in sorted(includes):
            result = add_include(result, include)
        if result != text:
            write(path, result, dry_run)
            print(f'{path}: + {", ".join(sorted(includes))}')


# --- wide-params ----------------------------------------------------------

NOT_VIABLE = re.compile(r"candidate function not viable: no known conversion from '(?:const )?wchar_t[^']*' "
                        r"to '(?:const )?(?:UINT16|unsigned short) \*'[^;]* for (\d+)\w\w argument")


def split_params(text, start):
    """Return (end, [(param_start, param_end)]) for the parameter list opening at start."""
    depth, index, spans, begin = 0, start, [], start + 1
    while index < len(text):
        char = text[index]
        if char in '([{<':
            depth += 1 if char != '<' else 0
            if char == '(' and depth == 1:
                begin = index + 1
        elif char in ')]}':
            depth -= 1
            if depth == 0:
                spans.append((begin, index))
                return index, spans
        elif char == ',' and depth == 1:
            spans.append((begin, index))
            begin = index + 1
        index += 1
    return index, spans


def pass_wide_params(entries, files, dry_run):
    targets = collections.defaultdict(set)
    for entry in entries:
        if entry['kind'] != 'note':
            continue
        match = NOT_VIABLE.search(entry['message'])
        if not match:
            continue
        path = relative(entry['file'])
        if not is_project_file(path):
            continue
        line = read(path).split('\n')[entry['line'] - 1]
        name = re.search(r'(\w+)\s*\(', line[entry['col'] - 1:])
        if name:
            targets[name.group(1)].add(int(match.group(1)) - 1)
    if not targets:
        return
    paths = files or [os.path.relpath(os.path.join(d, n), ROOT)
                      for base in ('src/sgp', 'src/wiz8', 'include/wiz8')
                      for d, _, names in os.walk(os.path.join(ROOT, base))
                      for n in names if n.endswith(('.cpp', '.c', '.h', '.H', '.hpp'))]
    declaration = re.compile(r'(?m)^[ \t]*(?:extern\s+)?(?:static\s+)?[\w:<>\*\s]+?\b(' +
                             '|'.join(map(re.escape, targets)) + r')\s*\(')
    for path in sorted(paths):
        text = read(path)
        result = text
        offset = 0
        for match in declaration.finditer(text):
            name = match.group(1)
            open_paren = match.end() - 1
            _, spans = split_params(text, open_paren)
            for index in sorted(targets[name]):
                if index >= len(spans):
                    continue
                begin, end = spans[index]
                param = text[begin:end]
                fixed = re.sub(r'\b(UINT16|unsigned short)(\s*\*)', r'CHAR16\2', param, count=1)
                if fixed != param:
                    result = result[:begin + offset] + fixed + result[end + offset:]
                    offset += len(fixed) - len(param)
        if result != text:
            write(path, result, dry_run)
            print(f'{path}: CHAR16 parameters')


# --- wide-vars ----------------------------------------------------------

UINT16_ARG = re.compile(r"no known conversion from '(?:const )?(?:UINT16|unsigned short)[^']*'(?: \(aka [^)]*\))? "
                        r"to '(?:const )?(?:CHAR16|wchar_t) \*'[^;]* for (\d+)\w\w argument")


def argument_name(expression):
    """The variable or member an argument expression names, if it is that simple."""
    expression = re.sub(r'^\s*&', '', expression.strip())
    expression = re.sub(r'\[[^\]]*\]\s*$', '', expression)
    match = re.fullmatch(r'(?:[\w\[\]]+(?:->|\.))*([A-Za-z_]\w*)', expression.replace(' ', ''))
    return match.group(1) if match else None


def retype(text, start, end, name):
    """Respell UINT16 declarations of name between start and end as CHAR16."""
    pattern = re.compile(r'\b(UINT16|unsigned short)\b(?=[^;(){}]*?\b' + re.escape(name) + r'\b\s*(?:\[|;|=|,|\)))')
    region = text[start:end]
    fixed = pattern.sub('CHAR16', region)
    return text[:start] + fixed + text[end:], fixed != region


def pass_wide_vars(entries, files, dry_run):
    by_file = collections.defaultdict(list)
    for index, entry in enumerate(entries):
        if entry['kind'] != 'error' or not entry['message'].startswith('no matching function'):
            continue
        for note in entries[index + 1:index + 8]:
            if note['kind'] != 'note':
                break
            match = UINT16_ARG.search(note['message'])
            if match:
                by_file[relative(entry['file'])].append((entry['line'], entry['col'], int(match.group(1)) - 1))
                break
    members = set()
    for path, calls in sorted(by_file.items()):
        if not is_project_file(path) or (files and path not in files):
            continue
        text = read(path)
        result, changed = text, False
        for line, col, argument in calls:
            lines = result.split('\n')
            call = sum(len(current) + 1 for current in lines[:line - 1]) + col - 1
            open_paren = result.find('(', call)
            _, spans = split_params(result, open_paren)
            if argument >= len(spans):
                continue
            name = argument_name(result[spans[argument][0]:spans[argument][1]])
            if name is None:
                continue
            function_start = max(result.rfind('\n{', 0, call), 0)
            result, local = retype(result, function_start, call, name)
            changed |= local
            if not local:
                members.add(name)
        if changed:
            write(path, result, dry_run)
            print(f'{path}: CHAR16 locals')
    if not members:
        return
    for base in ('src/sgp', 'include/wiz8', 'src/wiz8'):
        for directory, _, names in os.walk(os.path.join(ROOT, base)):
            for file_name in names:
                path = os.path.relpath(os.path.join(directory, file_name), ROOT)
                if not file_name.endswith(('.h', '.H', '.hpp', '.cpp')) or not is_project_file(path):
                    continue
                text = read(path)
                result = text
                for name in members:
                    declaration = re.compile(r'(?m)^(\s*(?:extern\s+|static\s+)?)(UINT16)(\s*\*?\s*' +
                                             re.escape(name) + r'\s*[\[;=])')
                    result = declaration.sub(r'\1CHAR16\3', result)
                if result != text:
                    write(path, result, dry_run)
                    print(f'{path}: CHAR16 members')


# --- rename ---------------------------------------------------------------

def pass_rename(entries, files, dry_run):
    renames = load_rules()['renames']
    paths = files or [os.path.relpath(os.path.join(d, n), ROOT)
                      for base in ('src/sgp', 'src/wiz8', 'include/wiz8')
                      for d, _, names in os.walk(os.path.join(ROOT, base))
                      for n in names if n.endswith(('.cpp', '.c', '.h', '.H', '.hpp', '.inc'))]
    skip = set(load_rules().get('rename_skip', [])) | set(load_rules()['platform_units'])
    for path in sorted(paths):
        if path in skip:
            continue
        text = read(path)
        tokens = TOKEN.findall(text)
        changed = False
        for index, token in enumerate(tokens):
            if token in renames:
                tokens[index] = renames[token]
                changed = True
        if changed:
            write(path, ''.join(tokens), dry_run)
            print(f'{path}: renamed')


# --- abi-asserts ----------------------------------------------------------

def pass_abi_asserts(entries, files, dry_run):
    raw_io = set(load_rules().get('raw_io_records', []))
    hits = collections.defaultdict(set)
    for entry in entries:
        if entry['kind'] == 'error' and entry['message'].startswith('static assertion failed'):
            hits[relative(entry['file'])].add(entry['line'])
    for path, lines in sorted(hits.items()):
        if not is_project_file(path) or (files and path not in files):
            continue
        source = read(path).split('\n')
        changed = False
        for number in sorted(lines):
            index = number - 1
            while index >= 0 and 'static_assert' not in source[index] and 'W8_ABI_ASSERT' not in source[index]:
                index -= 1
            if index < 0 or 'W8_ABI_ASSERT' in source[index]:
                continue
            statement = ' '.join(source[index:index + 3])
            subject = re.search(r'(?:sizeof|offsetof)\s*\(\s*(\w+)', statement)
            if subject and subject.group(1) in raw_io:
                print(f'{path}:{number}: raw I/O record {subject.group(1)} must keep its layout', file=sys.stderr)
                continue
            source[index] = source[index].replace('static_assert', 'W8_ABI_ASSERT', 1)
            changed = True
        if changed:
            write(path, '\n'.join(source), dry_run)
            print(f'{path}: layout checks gated')


PASSES = {
    'includes': pass_includes,
    'wide-params': pass_wide_params,
    'wide-vars': pass_wide_vars,
    'rename': pass_rename,
    'abi-asserts': pass_abi_asserts,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pass_name', choices=sorted(PASSES))
    parser.add_argument('files', nargs='*')
    parser.add_argument('--probe', default=os.path.join(ROOT, 'build-native', 'probe.jsonl'))
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    entries = load_probe(args.probe) if args.pass_name != 'rename' else []
    PASSES[args.pass_name](entries, set(args.files), args.dry_run)


if __name__ == '__main__':
    main()
