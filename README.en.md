# WE-Proton

[中文](README.md) | English

## About

On Linux (Wine / Proton), the WeGame client fails to download games, places windows in the wrong spot, misbehaves on input and shows black screens. This project fixes the Wine / Proton / vkd3d-proton bugs behind those problems on top of DWProton. The goal is to make the WeGame client itself, and games without kernel-level anti-cheat (single-player games, for example), run properly.

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
3. Select `we-proton-<version>` in the launcher. wegame-launcher uses the newest DWProton by default, so pick it by hand under Settings → Runner in the GUI, or run

   ```
   wegame-launcher runner we-proton-<version>
   ```

Notes:

- The prefix version is the same as that of the DWProton release it is based on, so switching between the two does not trigger a prefix upgrade
- 0005 needs userfaultfd asynchronous write-protection, available in Linux 6.7 and later; on older kernels that fix is inactive and everything else works as usual
- The rebuilt `d3d12.dll` and `d3d12core.dll` use the UCRT (`api-ms-win-crt-*`) while the official ones use `msvcrt.dll`; Wine provides both

## Building from source

```
git clone https://github.com/SHORiN-KiWATA/we-proton.git
cd we-proton
git submodule update --init wine Vulkan-Headers
git submodule update --init --recursive vkd3d-proton
we/overlay-build.sh --release <N>             # output in build/
we/overlay-build.sh --release <N> --install   # also install to ~/.local/share/proton/runners/WE-Proton
```

The script's steps, the test programs, the diagnostic tools and how to follow new upstream releases are described in [`we/README.md`](we/README.md) (in Chinese).

The full build (`make redist`, compiling every component from scratch in a container) is the same as upstream (not tested by this project); see the [DWProton](https://dawn.wine/dawn-winery/dwproton) and [Proton](https://github.com/ValveSoftware/Proton) documentation.

## Repository layout

- `patches/wine/`, `patches/vkd3d-proton/`: the patches. The `wine/` and `vkd3d-proton/` submodules stay at the upstream commits and the build applies the patches
- `we/fixes/`: one report per patch (symptoms, root cause, fix, verification)
- `we/tests/`: test programs and their output before and after each fix
- `we/diag/`: diagnostic patches and scripts, not part of the release build
- `we/overlay-build.sh`: the quick build script
- Everything else is DWProton's / Proton's own content

## Source and licenses

- Proton's top-level contents: BSD-3-Clause (Valve Corporation, see [`LICENSE`](LICENSE) and [`LICENSE.proton`](LICENSE.proton))
- vkd3d-proton: upstream vkd3d-proton plus the patches in `patches/vkd3d-proton/`, LGPL-2.1; its dxil-spirv subproject, and the patches to it, are MIT
- Other components: see the `LICENSE` / `COPYING` files in their directories, and `LICENSE`, `LICENSE.OFL` and `PATENTS.AV1` in the release package
- The scripts and documents under `we/` are under BSD-3-Clause as well

## Acknowledgements

- [Proton](https://github.com/ValveSoftware/Proton) by Valve and CodeWeavers
- [DWProton](https://dawn.wine/dawn-winery/dwproton) by Dawn Winery
- [Wine](https://www.winehq.org/), [DXVK](https://github.com/doitsujin/dxvk), [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) and the other projects Proton is built from
