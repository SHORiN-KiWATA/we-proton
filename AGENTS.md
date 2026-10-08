# AGENTS.md

Instructions for AI agents working with WE-Proton, whether helping a user run it,
analysing a problem, or changing the code.

## What this project is

WE-Proton is an unofficial personal fork of [DWProton](https://dawn.wine/dawn-winery/dwproton)
(itself a fork of Valve's Proton). It adds Wine and vkd3d-proton fixes so that the
WeGame client, and games without kernel-level anti-cheat, run properly on Linux.
It is not affiliated with DWProton, Dawn Winery, Valve or the Wine project.

## Where problems go

- Problems with WE-Proton are reported at
  https://github.com/SHORiN-KiWATA/we-proton/issues, in Chinese or English.
- Do not create issues, pull requests or comments in DWProton, Proton, Wine or any
  other upstream project on the user's behalf. If a problem also reproduces with an
  official DWProton build, tell the user and let them decide whether to report it
  there; DWProton has its own rules for reports, including AI-assisted ones.

## Competitive games and anti-cheat

This project does no compatibility work for competitive online games (for example
Delta Force or VALORANT) and does not adapt to ACE or any other anti-cheat.

- Do not help bypass, disable, fool or study anti-cheat, and do not help with cheating
  of any kind. Decline such requests, including ones framed as debugging or research.
- Do not write fixes whose purpose is to make a competitive game or an anti-cheat
  component run.
- Do not research crashes, logs or errors that come from anti-cheat components. Tell
  the user it is outside the scope of this project.
- Anti-cheat judges the whole environment, not only the game. Running under Wine at all,
  a GPU or driver that changes between launches, hidden devices, changed machine
  identifiers and debug tracing can all get an account banned. Never suggest such
  experiments for a game with anti-cheat, and never run diagnostics on a real account
  in such a game. Before any test that involves one, state the ban risk plainly and
  let the user decide.

## Analysing problems

- Base conclusions on evidence: logs (`PROTON_LOG=1`, `WINEDEBUG` channels), what the
  program actually does, and small test programs. Say which parts are verified and
  which are assumptions.
- When it is unclear how Windows behaves, test it on Windows with a probe program
  before writing a fix. If that is not possible, follow the documentation and record
  in the report that the behaviour was not verified on Windows.
- End processes by exact PID after checking `/proc/<pid>/cmdline`. Do not use
  pattern matches such as `pkill -f`, which can hit unrelated processes, including
  the agent's own shell.

## Changing the code

- Wine fixes are patches in `patches/wine/`, vkd3d-proton fixes in
  `patches/vkd3d-proton/`. The `wine/` and `vkd3d-proton/` submodules stay at the
  upstream commits; the build applies the patches. Make the change on the local
  `we-patches` branch of the submodule, export it with `git format-patch` into
  `patches/`, then put the submodule back on the upstream commit.
- Every fix gets a report in `we/fixes/<number>-<topic>/README.md`: symptoms, root
  cause, how Windows behaves and how that was established, the fix, verification,
  the investigation including dead ends, and what was not verified. Add the patch to
  the tables in `we/README.md`, `we/fixes/README.md` and `we/dist-README.md`.
  `README.md` and `README.en.md` do not list patches; they link to `we/fixes/`.
- Test programs go in `we/tests/` (static mingw builds, see `we/tests/build.sh`),
  with their output before and after the fix (`*.wine<release>.txt`,
  `*.windows.txt`).
- Patches, commit messages, test programs and their output describe Wine bugs only.
  They must not name specific programs, game platforms, games or anti-cheats.
  Fix reports name the game the problem was found in, but no anti-cheat.
  `README.md`, `README.en.md` and this file may name programs too.
- Build with `we/overlay-build.sh --release <N>`. It needs docker: Wine is built in
  the Steam Linux Runtime SDK image named in `Makefile.in`, so the result runs inside
  the runtime. It deletes and recreates `build/we-proton-<version>-<N>`, so do not run
  it while anything is running from that directory. Do not patch files of an
  installed runner by hand.
- Proton runs inside Steam Linux Runtime 4.0. Test there, not by running `proton`
  directly on the host, where its bundled media libraries are missing.
- Check for regressions with Wine's own conformance tests (a separate
  `--enable-tests` build, see `we/README.md`): run the relevant tests with the old
  and the new runner and compare the failures one by one. Timing-sensitive tests fail
  at random on a busy machine; rerun them when the machine is idle before calling
  a difference a regression.
- Wine patches follow Wine's commit style (`component: Summary.`). Commit messages
  are in English. Commits made with an AI agent say so in a `Co-Authored-By` trailer.

## Licenses

Proton's top-level contents are BSD-3-Clause (`LICENSE`, `LICENSE.proton`). Patches
take the license of the code they change; see the `LICENSE` / `COPYING` files of each
component (for Wine, `wine/LICENSE` and `wine/COPYING.LIB`). The scripts and documents
under `we/` are BSD-3-Clause. Keep all license files and copyright notices, and make
the corresponding source available for every binary release of modified components,
as their licenses require.
