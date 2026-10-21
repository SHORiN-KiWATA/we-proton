#!/usr/bin/env python3
"""List the commits GE-Proton or its Wine already carries, by subject.

    we/sync/dup-titles.py [--remove] <git directory> <revision range>

Prints every commit in <revision range> of the repository at <git directory>
whose subject equals the subject of a GE-Proton patch (patches/ of this
repository, without patches/dwproton, patches/wine and any disabled/ directory)
or of a wine-staging patch (wine-staging/patches/), or of a commit in the
history of the Wine commit the wine submodule is pinned to (Valve's Wine,
which picks up upstream Wine and DW-Proton changes over time). Patch subjects
wrapped over several lines are joined first. Output: position in the range (1 = oldest),
short hash, subject and the GE-Proton patch (or the Wine commit) with the same
subject. Exits with
status 1 when it finds any.

--remove also deletes (git rm) the patches/dwproton/ file with the same subject
as each commit found, and prints the deleted file names.

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
    argv = sys.argv[1:]
    remove = bool(argv) and argv[0] == "--remove"
    if remove:
        argv = argv[1:]
    if len(argv) != 2:
        sys.exit(__doc__)
    repo, rng = argv
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                          cwd=os.path.dirname(os.path.abspath(__file__)),
                          check=True, capture_output=True, text=True).stdout.strip()
    if not os.path.isdir(os.path.join(root, "wine-staging", "patches")):
        sys.exit("wine-staging/ is not checked out; run git submodule update --init wine-staging")
    ge = subjects(root)
    wine = subprocess.run(["git", "ls-tree", "HEAD", "wine"], cwd=root, check=True,
                          capture_output=True, text=True).stdout.split()[2]
    for line in subprocess.run(["git", "-C", os.path.join(root, "wine"), "log", "--no-merges",
                                "--format=%h%x09%s", wine], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        h, subj = line.split("\t", 1)
        ge.setdefault(subj, f"Wine {h}")
    log = subprocess.run(["git", "-C", repo, "log", "--reverse", "--topo-order", "--no-merges",
                          "--format=%h%x09%s", rng], check=True, capture_output=True,
                         text=True).stdout.splitlines()
    dw = {}
    for f in sorted(glob.glob(os.path.join(root, "patches", "dwproton", "*.patch"))):
        text = open(f, errors="replace").read(20000)
        m = re.search(r"^Subject: (.*(?:\n[ \t].*)*)", text, re.M)
        if m:
            subj = re.sub(r"^\[PATCH[^\]]*\]\s*", "", re.sub(r"\s*\n[ \t]+", " ", m.group(1)).strip())
            dw.setdefault(subj, f)
    hits = 0
    for i, line in enumerate(log, 1):
        h, s = line.split("\t", 1)
        if s in ge:
            hits += 1
            print(f"{i:04d} {h} {s}\n     = {ge[s]}")
            if remove:
                if s in dw:
                    subprocess.run(["git", "rm", "-q", dw[s]], cwd=root, check=True)
                    print(f"     removed {os.path.relpath(dw[s], root)}")
                else:
                    print("     (no patches/dwproton file has this subject)")
    print(f"{hits} of {len(log)} commits have the subject of a GE-Proton or wine-staging patch"
          " or of a commit in GE-Proton's Wine")
    sys.exit(1 if hits else 0)


if __name__ == "__main__":
    main()
