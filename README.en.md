# WE-Proton

[中文](README.md) | English

## About

On Linux (Wine / Proton), the WeGame client fails to download games, places windows in the wrong spot, misbehaves on input and shows black screens. This project is based on GE-Proton and DW-Proton: it starts from GE-Proton, adds DW-Proton's patches (compatibility fixes for anime games and others), and fixes the Wine / Proton / vkd3d-proton bugs behind those problems. The goal is to make the WeGame client itself, and games without kernel-level anti-cheat (single-player games, for example), run properly.

Companion launcher: [wegame-launcher](https://github.com/SHORiN-KiWATA/wegame-launcher) installs and runs WeGame in one step.

## About AI

The patches added by this project were written and debugged with the help of AI. The symptoms, root cause and verification of each patch are documented in [`we/fixes/`](we/fixes/) (in Chinese).

## Competitive games and anti-cheat

- This project does not fix or support competitive games such as Delta Force or VALORANT. Issues about them will be closed.
- This project does not adapt to ACE or any other anti-cheat, and does not accept requests or code that bypass anti-cheat or relate to cheating.
- Linux is not a platform supported by ACE. Even if a game with ACE runs, the account may still be banned for an abnormal environment. You bear the consequences, including bans, of using this project.

## Demo

https://github.com/user-attachments/assets/42438b93-5a36-4557-8a0f-4b65b53d5e07

## Installation

1. Download `we-proton-<version>.tar.xz` from [Releases](https://github.com/SHORiN-KiWATA/we-proton/releases)
2. Extract it into your launcher's Proton directory:
   - wegame-launcher: `~/.local/share/proton/runners/`
   - Steam: `~/.local/share/Steam/compatibilitytools.d/`
   - Lutris, Heroic and others: their own Proton / runner directory
3. Select `we-proton-<version>` in the launcher. wegame-launcher uses the newest WE-Proton it finds by default; to pick one by hand, use Settings → Runner in the GUI, or run

   ```
   wegame-launcher runner we-proton-<version>
   ```

Notes:

- The prefix version is the same as that of the GE-Proton release it is based on (`GE-Proton11-7`). Coming from an older WE-Proton based on DW-Proton (`we-proton-11.0-14-*`), the prefix is upgraded once. The log then says `Prefix has an invalid version?!`: GE-Proton's script doesn't understand versions starting with `dwproton-`, skips its own upgrade steps and leaves the update to Wine, which is fine
- 0005 needs userfaultfd asynchronous write-protection, available in Linux 6.7 and later; on older kernels that fix is inactive and everything else works as usual

## Building from source

Needs docker. The first full build takes a few hours:

```
git clone https://github.com/SHORiN-KiWATA/we-proton.git
cd we-proton
we/build.sh --name we-proton-<version>             # output in build/
we/build.sh --name we-proton-<version> --install   # also install to "~/.local/share/proton/runners/WE-Proton Latest"
```

The script fetches the submodules, patches them with `patches/protonprep-valve-staging.sh` (GE-Proton's patches, then DW-Proton's and WE-Proton's) and runs `make redist` in the Steam Linux Runtime SDK container. The details, the test programs, the diagnostic tools and how to follow new upstream releases are described in [`we/README.md`](we/README.md) (in Chinese).

## Repository layout

- `patches/wine/`, `patches/vkd3d-proton/we-*.patch`: WE-Proton's patches
- `patches/dwproton/`: Wine patches DW-Proton has and GE-Proton doesn't
- The rest of `patches/` and `patches/protonprep-valve-staging.sh`: GE-Proton's patches and patching script (`protonfixes/0003`–`0005` and `dxvk/dw-*` come from DW-Proton). The submodules stay at the upstream commits; the script applies all patches before the build
- `we/fixes/`: one report per patch (symptoms, root cause, fix, verification)
- `we/tests/`: test programs and their output before and after each fix
- `we/diag/`: diagnostic patches and scripts, not part of the release build
- `we/build.sh`: the build script
- `we/sync/`, `we/upstream-sync.md`: scripts and a step-by-step guide for following new GE-Proton and DW-Proton releases
- Everything else is GE-Proton's / Proton's own content

## Source and licenses

- Proton's top-level contents: BSD-3-Clause (Valve Corporation, see [`LICENSE`](LICENSE) and [`LICENSE.proton`](LICENSE.proton))
- Wine: Valve's Wine plus the patches in `patches/`, LGPL-2.1
- vkd3d-proton: upstream vkd3d-proton plus the patches in `patches/vkd3d-proton/`, LGPL-2.1; the dxil-spirv subproject and the patch to it are MIT
- Other components: see the `LICENSE` / `COPYING` files in their directories, and `LICENSE`, `LICENSE.OFL` and `PATENTS.AV1` in the release package
- The scripts and documents under `we/` are under BSD-3-Clause as well

## Acknowledgements

- [Proton](https://github.com/ValveSoftware/Proton) by Valve and CodeWeavers
- [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom) by GloriousEggroll
- [DW-Proton](https://dawn.wine/dawn-winery/dwproton) by Dawn Winery
- [Wine](https://www.winehq.org/), [DXVK](https://github.com/doitsujin/dxvk), [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) and the other projects Proton is built from
