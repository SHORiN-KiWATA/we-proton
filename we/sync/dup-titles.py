#!/usr/bin/env python3
"""List the commits GE-Proton already carries, by subject.

    we/sync/dup-titles.py <git directory> <revision range>

Prints every commit in <revision range> of the repository at <git directory>
whose subject equals the subject of a GE-Proton patch (patches/ of this
repository, without patches/dwproton, patches/wine and any disabled/ directory)
or of a wine-staging patch (wine-staging/patches/). Patch subjects wrapped over
several lines are joined first. Output: position in the range (1 = oldest),
short hash, subject and the GE-Proton patch with the same subject. Exits with
status 1 when it finds any.

Examples:
    we/sync/dup-titles.py wine we-base..we-dw
    we/sync/dup-titles.py ~/src/wine-dwproton b585905ec92..<new DW-Proton wine commit>
"""
import glob, os, re, subprocess, sys


def subjects(root):
    found = {}
    pats = glob.glob(os.path.join(root, "patches", "**", "*.patch"), recursive=True)
    pats += glob.glob(os.path.join(root, "wine-staging", "patches", "**", "*.patch"), recursive=True)
    for p in pats:
        rel = os.path.relpath(p, root)
        if "/disabled/" in rel or rel.startswith(("patches/dwproton/", "patches/wine/")):
            continue
        text = open(p, errors="replace").read(20000)
        m = re.search(r"^Subject: (.*(?:\n[ \t].*)*)", text, re.M)
        if not m:
            continue
        s = re.sub(r"\s*\n[ \t]+", " ", m.group(1)).strip()
        s = re.sub(r"^\[PATCH[^\]]*\]\s*", "", s)
        found.setdefault(s, rel)
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    repo, rng = sys.argv[1], sys.argv[2]
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                          cwd=os.path.dirname(os.path.abspath(__file__)),
                          check=True, capture_output=True, text=True).stdout.strip()
    if not os.path.isdir(os.path.join(root, "wine-staging", "patches")):
        sys.exit("wine-staging/ is not checked out; run git submodule update --init wine-staging")
    ge = subjects(root)
    log = subprocess.run(["git", "-C", repo, "log", "--reverse", "--topo-order", "--no-merges",
                          "--format=%h%x09%s", rng], check=True, capture_output=True,
                         text=True).stdout.splitlines()
    hits = 0
    for i, line in enumerate(log, 1):
        h, s = line.split("\t", 1)
        if s in ge:
            hits += 1
            print(f"{i:04d} {h} {s}\n     = {ge[s]}")
    print(f"{hits} of {len(log)} commits have the subject of a GE-Proton or wine-staging patch")
    sys.exit(1 if hits else 0)


if __name__ == "__main__":
    main()
