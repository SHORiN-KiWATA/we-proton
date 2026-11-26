<!-- SPDX-License-Identifier: BSD-3-Clause -->
# Optional UU / native Steam shared-network entrypoints

These three scripts let an installed accelerator and native Linux Steam use the
same network session managed by
[shorin-proton-wrapper](https://github.com/SHORiN-KiWATA/proton-wrapper):

- `uu-network-launch`: launch the installed accelerator in its dedicated prefix.
- `uu-network-run COMMAND [ARG ...]`: launch a native command in that prefix's
  network, preserving its existing game/runtime environment.
- `uu-network-steam [OPTION OR URI ...]`: launch or contact native Steam there,
  refusing an existing native client found in another network namespace.

This is optional integration code. It does not modify the runtime's default
launch behavior, install the client, change game launch options, stop processes,
or patch a running namespace. The user's Steam font setup is not a dependency.
The scripts never select a game/node or declare that acceleration succeeded.

## Requirements

- Linux, Python 3.10+, Bash, and native Linux Steam. Flatpak/Snap Steam have not
  been validated with this integration.
- An independently installed `shorin-proton-wrapper` supporting persistent
  per-prefix networking and native Linux targets. This code depends on those
  behaviors; the wrapper is not included here.
- A suitable `pasta` from [passt](https://passt.top/passt), the wrapper's other
  namespace/runtime dependencies, and a compatible installed Proton runner.
  In the original host setup, the distro pasta lacked `--no-pidns`; the working
  local dependency was built from passt commit
  `6aa4f88bd9846411edff3ed70460475b538addca`. No host-built binary is distributed
  by this directory. Check the selected wrapper's documented requirements.
- An installed client in a dedicated prefix, with the wrapper's `--netns on`
  setting already enabled. These scripts reject an absent/disabled setting
  instead of allowing the wrapper's native host-network fallback.

The scripts do not set up kernel/network permissions or relax system security
policy. Use the wrapper's normal installation and documented setup process.

## Run directly, without installation

Keep this directory together; each executable imports `uu_network.py` beside it.
Copy `config.example.json` to a location you choose, then edit its runner and
wrapper paths. The runner field is required and must name the actual `proton`
executable. Do not leave `YOUR-TESTED-RUNNER` in the path.

From this directory, using an absolute path to your edited configuration:

```sh
UU_NETWORK_CONFIG=/absolute/path/config.json ./uu-network-launch
UU_NETWORK_CONFIG=/absolute/path/config.json ./uu-network-steam
UU_NETWORK_CONFIG=/absolute/path/config.json ./uu-network-steam 'steam://open/library'
UU_NETWORK_CONFIG=/absolute/path/config.json ./uu-network-run /usr/bin/readlink /proc/self/ns/net
```

Before first use, enable the dedicated prefix's network setting through your
wrapper, for example (replace paths with the same ones used in the configuration):

```sh
/absolute/path/shorin-proton-wrapper -p /absolute/path/UU-Accelerator --netns on
```

This explicit setup changes the wrapper's prefix configuration. The launchers
do not perform it for you. A correctly configured wrapper may create its network
session when a command or the Steam namespace probe joins it. Therefore the
probe is bounded but is not guaranteed to be a read-only `/proc` operation.

Start acceleration in the client, then use `uu-network-steam` to open the game
library. If Steam already uses another network, exit it normally from its own
menu and reopen through this entrypoint. Nothing is automatically terminated.
Changing the selected accelerator target/node remains the user's action.

## Configuration

The default file is `$XDG_CONFIG_HOME/uu-network/config.json`, or
`~/.config/uu-network/config.json` when `XDG_CONFIG_HOME` is unset. Set
`UU_NETWORK_CONFIG` to use another file. An explicitly selected missing or invalid
file is an error. Without a default file, environment-only configuration works.

Every field is a string. Unknown fields are rejected. Explicit environment values
override file values, followed by the defaults below. Paths must be absolute or
start with `~/`; arbitrary variable/command expansion is never performed. Empty
explicit path values are errors, rather than instructions to choose another file.

| JSON field | Environment override | Default / purpose |
| --- | --- | --- |
| `prefix` | `UU_PREFIX` | `~/Games/UU-Accelerator` |
| `runner` | `UU_RUNNER` | Required path to `proton`; never auto-select a newest version |
| `wrapper` | `UU_WRAPPER` | Resolve `shorin-proton-wrapper` on `PATH` |
| `steam_real` | `STEAM_REAL` | `/usr/bin/steam`; one executable path, not a command string |
| `uu_executable` | `UU_EXE` | `pfx/drive_c/Program Files (x86)/Netease/UU/uu_launcher.exe` under the chosen prefix |
| `pasta_bin_dir` | `UU_PASTA_BIN_DIR` | Optional directory containing a suitable `pasta`; otherwise use `PATH` |
| `uu_locale` | `UU_LOCALE` | Optional `LANG`/`LC_ALL` override for the accelerator only; otherwise preserve the current locale |
| `desktop_wrapper` | `UU_DESKTOP_WRAPPER` | Optional desktop shortcut integration executable for the accelerator only |

Only the accelerator entrypoint supplies the previous setup's defaults
`PROTON_NO_FSYNC=1`, `PROTON_USE_WOW64=1`, and `PROTON_USE_XALIA=0`. Existing explicit
values for those variables take precedence. They are compatibility choices for
that client/prefix, not recommended overrides for all games. The native network
and Steam entrypoints neither add nor remove per-game Wine/Proton settings.
They only optionally prepend the configured pasta directory to `PATH`.

`STEAM_REAL` can point to an existing user-owned Steam wrapper if desired, but
must eventually execute the real client. Direct/symlink references back to one of
these entrypoints are rejected. Arbitrary indirect recursion in another custom
script cannot be inferred; do not configure a wrapper that calls `uu-network-steam`.

## Optional personal installation

No installer is provided. Running from the directory avoids overwriting existing
launchers. If you want a stable location, copy this directory to a **new**
`~/.local/lib/uu-network-integration` directory after confirming it does not already
contain your own files, and keep its Python module with the three entrypoints.
You may create dedicated `~/.local/bin/uu-network-launch`,
`~/.local/bin/uu-network-run`, and `~/.local/bin/uu-network-steam` links to those
files, provided those names are unused. Do not replace `~/.local/bin/steam` or
`/usr/bin/steam` with this integration.

For a menu entry, use the absolute path of `uu-network-steam` in a separate
user-created `.desktop` file and configure the default JSON location above.
If adapting an existing Steam menu entry or URI association, first save its
original content/association and preserve its actions and `%U`/`%u` argument
handling. Desktop Entry quoting is not shell quoting: do not paste a shell command
with environment assignments into `Exec=`. This release does not automatically
replace menu entries, URI handlers, autostart entries or game shortcuts.

To stop using it, use the original Steam entry after exiting Steam normally.
Remove only the links/files you created; restore any menu/URI choice you changed.
No uninstall command kills applications, deletes a prefix, or tears down a live
network. The existing process remains in its current network until it exits.

## Why the command is quoted once

The wrapper scans arguments for nested `proton run` / `proton waitforexitandrun`
sequences. Passing a game command as ordinary outer arguments can accidentally
select another prefix. The native entrypoint gives it a single `bash -c` command
constructed with `shlex.join`, preserving each original argument literally.
Empty arguments, quotes, Unicode, newlines and literal shell substitutions are
covered by tests. Do not replace this with `$*`, `eval`, or unquoted interpolation.

The Steam gate checks same-user native executables and their network namespaces,
then checks again after the probe. It ignores script/interpreter gates and vanished
processes, and refuses unreadable or contradictory results. This is not an atomic
lock shared with every possible Steam launcher: another external launch can race
the final exec. The code never resolves such races by closing another process.

## Verification and limits

Run the isolated source tests with Python 3.11+ (the entrypoints themselves support Python 3.10+):

```sh
python3 -B -m unittest discover -s tests -v
```

The original machine-specific entrypoints had 26 isolated tests plus separate
real-session checks of native Steam and ordinary runtime children. This portable
version migrates 25 of those checks directly. The old test that copied/executed
an installed upstream wrapper in a rewritten HOME is replaced with a narrower
wrapper-argv contract test: the new tests do not copy third-party code, change
HOME, or discover a real network keeper. Configuration, missing-network,
recursive-launcher and accelerator-only environment checks are added.

On 2026-11-03, all 45 isolated tests passed. The test suite uses temporary fixture executables/process trees and mocks. It
does not run Steam, the accelerator, Wine, pasta, a namespace tool, a game or an
account. No native Windows comparison is relevant to these Linux entrypoints,
and none is claimed. The portable configuration/layout has not yet been validated
in a new real-user network session. Historical runtime results are evidence for
the original setup, not proof of this new package on arbitrary distributions.

Standard descendants inherit a network namespace, but programs can deliberately
create another one or delegate to a host service. A selected accelerator route
also depends on the client's game/node policy. Namespace placement alone neither
proves a connection is accelerated nor establishes game compatibility.

## Source and license

New scripts, tests and documentation here are BSD-3-Clause under the repository's
`LICENSE` / `LICENSE.proton` and the scripts-under-`we/` license policy. Their
starting point is the local 2026-11 UU launcher, native network wrapper and Steam
single-instance guard; this version removes machine-specific paths and the font
launcher dependency. Shell quoting was translated from per-argument Bash `%q`
to Python `shlex.join` and re-tested using an actual Bash fixture execution.

No accelerator executable, prefix/account data, Steam binary, runtime binary,
pasta binary, or shorin-proton-wrapper source is bundled. Those dependencies keep
their own licenses and installation requirements.
