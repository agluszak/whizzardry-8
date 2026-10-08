#!/usr/bin/env python3
"""Spell the original code's 32-bit `long` as w8_long / w8_ulong.

MSVC's `long` is 32 bits; LP64 Linux and macOS make it 64. compiler.h maps
w8_long/w8_ulong to `long`/`unsigned long` in the legacy lanes (identical code
and mangled names) and to 32-bit integers in the native lane.

The rewrite only touches code tokens: comments, string and character literals
are copied unchanged. `long long` and `long double` are left alone. The
transformation is idempotent, so it can be re-run on files ported from the
decomp repository before merging them.

Usage: native_long_rewrite.py FILE...
"""
import re
import sys

TOKEN = re.compile(
    r'//[^\n]*'
    r'|/\*.*?\*/'
    r'|"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r'|[A-Za-z_][A-Za-z_0-9]*'
    r'|\s+'
    r'|.',
    re.S)


def rewrite(text):
    tokens = TOKEN.findall(text)
    out = []
    i = 0

    def next_word(j):
        """Index of the next identifier-like token after j, skipping whitespace."""
        j += 1
        while j < len(tokens) and tokens[j].isspace():
            j += 1
        return j

    while i < len(tokens):
        tok = tokens[i]
        if tok == 'unsigned' or tok == 'signed':
            j = next_word(i)
            if j < len(tokens) and tokens[j] == 'long':
                k = next_word(j)
                if k < len(tokens) and tokens[k] in ('long', 'double'):
                    out.extend(tokens[i:k + 1])
                    i = k + 1
                    continue
                if k < len(tokens) and tokens[k] == 'int':
                    j = k
                out.append('w8_ulong' if tok == 'unsigned' else 'w8_long')
                i = j + 1
                continue
        elif tok == 'long':
            j = next_word(i)
            if j < len(tokens) and tokens[j] in ('long', 'double'):
                out.extend(tokens[i:j + 1])
                i = j + 1
                continue
            if j < len(tokens) and tokens[j] in ('unsigned',):
                k = next_word(j)
                if k < len(tokens) and tokens[k] == 'int':
                    j = k
                out.append('w8_ulong')
                i = j + 1
                continue
            if j < len(tokens) and tokens[j] == 'int':
                out.append('w8_long')
                i = j + 1
                continue
            out.append('w8_long')
            i += 1
            continue
        out.append(tok)
        i += 1
    return ''.join(out)


def main(paths):
    changed = 0
    for path in paths:
        with open(path, encoding='latin-1', newline='') as handle:
            text = handle.read()
        result = rewrite(text)
        if result != text:
            with open(path, 'w', encoding='latin-1', newline='') as handle:
                handle.write(result)
            changed += 1
    print(f'{changed} of {len(paths)} files rewritten')


if __name__ == '__main__':
    main(sys.argv[1:])
