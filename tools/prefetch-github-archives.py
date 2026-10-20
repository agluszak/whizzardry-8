#!/usr/bin/env python3
"""Fill Conan's download cache with GitHub archive tarballs rebuilt via git.

Sandboxes that serve public GitHub repositories over git but block archive
downloads cannot fetch recipe sources such as
https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.tar.gz. GitHub
creates those with `git archive` and `gzip -n`, so they can be reproduced
byte for byte and stored under their checksum where Conan looks first.

Usage: prefetch-github-archives.py GRAPH_JSON DOWNLOAD_CACHE
GRAPH_JSON comes from `conan graph info ... --format=json`.
"""
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

ARCHIVE = re.compile(
    r"https://(?:github\.com/([^/]+)/([^/]+)/archive/(?:refs/tags/)?"
    r"|codeload\.github\.com/([^/]+)/([^/]+)/tar\.gz/(?:refs/tags/)?)"
    r"(.+?)(?:\.tar\.gz)?$")


def sources(graph):
    for node in graph["graph"]["nodes"].values():
        folder, version = node.get("recipe_folder"), node.get("version")
        data = Path(folder or "", "conandata.yml")
        if not folder or not data.exists():
            continue
        entries = (yaml.safe_load(data.read_text()) or {}).get("sources", {}).get(version)
        for entry in entries if isinstance(entries, list) else [entries]:
            if isinstance(entry, dict) and "sha256" in entry:
                urls = entry.get("url", [])
                yield entry["sha256"], urls if isinstance(urls, list) else [urls]


def rebuild(owner, repo, ref, sha256, target):
    # GitHub drops a leading "v" from version tags in the archive prefix.
    names = {ref, re.sub(r"^v(?=\d)", "", ref)}
    with tempfile.TemporaryDirectory() as clone:
        subprocess.run(["git", "init", "-q", "--bare", clone], check=True)
        subprocess.run(["git", "-C", clone, "fetch", "-q", "--depth=1",
                        f"https://github.com/{owner}/{repo}", ref], check=True)
        for name in names:
            tar = subprocess.run(
                ["git", "-C", clone, "archive", "--format=tar",
                 f"--prefix={repo}-{name}/", "FETCH_HEAD"],
                check=True, capture_output=True).stdout
            data = subprocess.run(["gzip", "-cn"], input=tar, check=True,
                                  capture_output=True).stdout
            if hashlib.sha256(data).hexdigest() == sha256:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                return True
    return False


def main(graph_json, cache):
    for sha256, urls in sources(json.loads(Path(graph_json).read_text())):
        target = Path(cache, "s", sha256)
        if target.exists():
            continue
        for url in urls:
            match = ARCHIVE.match(url)
            if not match:
                continue
            owner, repo = match[1] or match[3], match[2] or match[4]
            if rebuild(owner, repo, match[5], sha256, target):
                print(f"Prefetched {url}")
            else:
                print(f"warning: {url} does not reproduce {sha256}", file=sys.stderr)
            break


if __name__ == "__main__":
    main(*sys.argv[1:])
