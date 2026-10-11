#!/usr/bin/env python3
"""Write the commits of the wine/ development branches back into patches/.

    we/sync/export-patches.py [--new NNNN]...

Run it from anywhere in the repository after we/sync/wine-dev.sh, with wine/
on we-dev and the branches we-base, we-dw and we-dev in place:

  we-base..we-dw   -> patches/dwproton/, numbered 0001, 0002, ... in order
  we-dw..we-dev    -> patches/wine/, each WE-Proton patch keeping its number: a
                      commit whose subject is that of an existing patches/wine/
                      file reuses the file's number. A commit with a new
                      subject takes the next number given with --new (in
                      order); without one, nothing is written.

Generated files are left out of the patches; protonprep's make_requests and
Makefile.in's make_specfiles regenerate them. Patches that are no longer
produced are deleted. Prints what changed.
"""
import argparse, os, re, subprocess, sys, tempfile

GENERATED = ["include/wine/server_protocol.h", "server/request_handlers.h",
             "server/request_trace.h", "dlls/ntdll/ntsyscalls.h",
             "dlls/win32u/win32syscalls.h"]


def git(*args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout


def subject_of(path):
    text = open(path, errors="replace").read(20000)
    m = re.search(r"^Subject: (.*(?:\n[ \t].*)*)", text, re.M)
    if not m:
        return None
    s = re.sub(r"\s*\n[ \t]+", " ", m.group(1)).strip()
    return re.sub(r"^\[PATCH[^\]]*\]\s*", "", s)


def format_patches(wine, rng, outdir):
    pathspec = ["--", "."] + [":!" + p for p in GENERATED]
    git("format-patch", "-q", "-N", "--no-signature", "-o", outdir, rng, *pathspec, cwd=wine)
    return sorted(os.listdir(outdir))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--new", action="append", default=[], metavar="NNNN",
                    help="number for the next WE-Proton commit that has no patch yet")
    args = ap.parse_args()
    for n in args.new:
        if not re.fullmatch(r"\d{4}", n):
            sys.exit(f"--new takes a four-digit number, not {n!r}")

    root = git("rev-parse", "--show-toplevel", cwd=os.path.dirname(os.path.abspath(__file__))).strip()
    wine = os.path.join(root, "wine")
    for b in ("we-base", "we-dw", "we-dev"):
        if subprocess.run(["git", "rev-parse", "-q", "--verify", b], cwd=wine,
                          capture_output=True).returncode:
            sys.exit(f"wine/ has no branch {b}; run we/sync/wine-dev.sh first")
    if git("status", "--porcelain", "--untracked-files=no", cwd=wine).strip():
        sys.exit("wine/ has uncommitted changes; commit them (or finish git am) first")

    dw_dir = os.path.join(root, "patches", "dwproton")
    we_dir = os.path.join(root, "patches", "wine")
    old_we = {}
    for f in os.listdir(we_dir):
        if f.endswith(".patch"):
            old_we[subject_of(os.path.join(we_dir, f))] = f

    with tempfile.TemporaryDirectory() as tmp:
        # WE-Proton patches: work out every name before writing anything
        we_tmp = os.path.join(tmp, "we")
        os.mkdir(we_tmp)
        new_numbers = list(args.new)
        we_names = []
        for f in format_patches(wine, "we-dw..we-dev", we_tmp):
            subj = subject_of(os.path.join(we_tmp, f))
            if subj in old_we:
                name = old_we[subj]
            else:
                if not new_numbers:
                    sys.exit(f"new WE-Proton commit without a number: {subj!r}; "
                             "pass --new NNNN (claim the number first)")
                name = new_numbers.pop(0) + f[4:]
            we_names.append((f, name))
        if new_numbers:
            sys.exit(f"unused --new numbers: {new_numbers}")
        if len({n[:4] for _, n in we_names}) != len(we_names):
            sys.exit("two WE-Proton patches would get the same number")

        dw_tmp = os.path.join(tmp, "dw")
        os.mkdir(dw_tmp)
        dw_files = format_patches(wine, "we-base..we-dw", dw_tmp)

        def replace(dest_dir, pairs, src_dir):
            before = {f: open(os.path.join(dest_dir, f), "rb").read()
                      for f in os.listdir(dest_dir) if f.endswith(".patch")}
            for f in before:
                os.remove(os.path.join(dest_dir, f))
            changed = []
            for src, name in pairs:
                data = open(os.path.join(src_dir, src), "rb").read()
                if name in before and strip_from(before[name]) == strip_from(data):
                    data = before[name]  # keep the old From line: no change at all
                elif name in before:
                    changed.append("changed  " + name)
                else:
                    changed.append("new      " + name)
                open(os.path.join(dest_dir, name), "wb").write(data)
            for f in before:
                if f not in {n for _, n in pairs}:
                    changed.append("removed  " + f)
            return changed

        changes = replace(dw_dir, [(f, f) for f in dw_files], dw_tmp)
        changes += replace(we_dir, we_names, we_tmp)

    print(f"patches/dwproton: {len(dw_files)} patches, patches/wine: {len(we_names)} patches")
    for c in changes:
        print("  " + c)
    if not changes:
        print("  no changes other than the commit hashes in the From lines")


def strip_from(data):
    # the "From <hash>" line changes whenever a branch is rebuilt
    return re.sub(rb"^From [0-9a-f]{40} ", b"From X ", data, count=1)


if __name__ == "__main__":
    main()
