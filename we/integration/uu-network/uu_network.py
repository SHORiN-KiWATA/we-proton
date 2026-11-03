# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 UU network integration contributors
"""Small user-scoped entrypoints for a wrapper-owned shared network session.

Derived from local UU/network/Steam launchers developed and tested in 2026-11.
No wrapper, runtime, accelerator client or networking binary is bundled here.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from typing import NamedTuple


DIRECTORY = Path(__file__).resolve().parent
ENTRYPOINTS = tuple(DIRECTORY / name for name in
                    ("uu-network-launch", "uu-network-run", "uu-network-steam"))
NETNS = re.compile(r"net:\[[0-9]+\]")
PROBE_TIMEOUT = 8
CONFIG_KEYS = {"prefix", "runner", "wrapper", "steam_real", "pasta_bin_dir",
               "uu_executable", "uu_locale", "desktop_wrapper"}


class LauncherError(Exception):
    def __init__(self, message: str, exit_code: int = 1):
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class Config:
    prefix: Path
    runner: Path
    wrapper: Path
    steam_real: Path
    uu_executable: Path
    pasta_bin_dir: Path | None = None
    uu_locale: str | None = None
    desktop_wrapper: Path | None = None


def _path(value: str, label: str) -> Path:
    if not value or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise LauncherError(f"Invalid {label} path in configuration.")
    if value == "~" or value.startswith("~/"):
        path = Path.home() / value[2:] if value != "~" else Path.home()
    else:
        path = Path(value)
    if not path.is_absolute():
        raise LauncherError(f"{label} must be an absolute path (or start with ~/).")
    return path.resolve()


def load_config(environ: dict[str, str] | None = None) -> Config:
    env = os.environ if environ is None else environ
    configured = env.get("UU_NETWORK_CONFIG")
    if "UU_NETWORK_CONFIG" in env:
        config_path = _path(configured, "UU_NETWORK_CONFIG")
    else:
        config_base = _path(env["XDG_CONFIG_HOME"], "XDG_CONFIG_HOME") if env.get("XDG_CONFIG_HOME") else Path.home() / ".config"
        config_path = config_base / "uu-network" / "config.json"
    data: dict[str, str] = {}
    try:
        if configured is not None or config_path.exists():
            raw = json.loads(config_path.read_text(encoding="utf-8"))
            if not isinstance(raw, dict) or set(raw) - CONFIG_KEYS:
                raise LauncherError("Configuration must be an object containing only documented fields.")
            if any(not isinstance(v, str) for v in raw.values()):
                raise LauncherError("Configuration values must be strings.")
            data = raw
    except (OSError, ValueError) as error:
        raise LauncherError("Cannot read UU network configuration as JSON.") from error

    def value(key: str, variable: str, default: str | None = None) -> str | None:
        return env.get(variable, data.get(key, default))

    def optional_path(key: str, variable: str) -> Path | None:
        setting = value(key, variable)
        return None if setting is None else _path(setting, variable)

    prefix = _path(value("prefix", "UU_PREFIX", str(Path.home() / "Games/UU-Accelerator")), "UU_PREFIX")
    runner_value = value("runner", "UU_RUNNER")
    if runner_value is None:
        raise LauncherError("Set UU_RUNNER or the runner configuration field to your Proton executable.")
    runner = _path(runner_value, "UU_RUNNER")
    wrapper_value = value("wrapper", "UU_WRAPPER")
    if wrapper_value is None:
        wrapper_value = shutil.which("shorin-proton-wrapper", path=env.get("PATH", os.defpath))
    if wrapper_value is None:
        raise LauncherError("Install shorin-proton-wrapper and set UU_WRAPPER if it is outside PATH.")
    locale = value("uu_locale", "UU_LOCALE")
    if locale is not None and (not locale or any(ord(c) < 32 or ord(c) == 127 for c in locale)):
        raise LauncherError("Invalid UU_LOCALE value.")
    return Config(prefix, runner, _path(wrapper_value, "UU_WRAPPER"),
                  _path(value("steam_real", "STEAM_REAL", "/usr/bin/steam"), "STEAM_REAL"),
                  _path(value("uu_executable", "UU_EXE", str(prefix / "pfx/drive_c/Program Files (x86)/Netease/UU/uu_launcher.exe")), "UU_EXE"),
                  optional_path("pasta_bin_dir", "UU_PASTA_BIN_DIR"), locale,
                  optional_path("desktop_wrapper", "UU_DESKTOP_WRAPPER"))


def executable(path: Path, label: str) -> None:
    if not path.is_file() or not os.access(path, os.X_OK):
        raise LauncherError(f"{label} is not an executable file.")
    if path.resolve() in {entry.resolve() for entry in ENTRYPOINTS}:
        raise LauncherError(f"{label} points back to a UU network entrypoint; refusing recursion.")


def network_environment(config: Config, environ: dict[str, str] | None = None) -> dict[str, str]:
    env = dict(os.environ if environ is None else environ)
    if config.pasta_bin_dir is not None:
        executable(config.pasta_bin_dir / "pasta", "Configured pasta")
        env["PATH"] = str(config.pasta_bin_dir) + os.pathsep + env.get("PATH", os.defpath)
    return env


def validate_network(config: Config, env: dict[str, str]) -> None:
    executable(config.wrapper, "UU_WRAPPER")
    executable(config.runner, "UU_RUNNER")
    try:
        lines = (config.prefix / ".shorin-proton-wrapper.conf").read_bytes().split(b"\n")
    except OSError as error:
        raise LauncherError("UU prefix has no readable network configuration. Enable its wrapper --netns on setting first.") from error
    if b"netns=1" not in lines:
        raise LauncherError("UU prefix networking is disabled. Enable its wrapper --netns on setting first.")
    if shutil.which("pasta", path=env.get("PATH", os.defpath)) is None:
        raise LauncherError("pasta is missing from PATH; install a suitable passt version.")


def network_argv(config: Config, arguments: list[str]) -> list[str]:
    if not arguments or not arguments[0]:
        raise LauncherError("Usage: uu-network-run COMMAND [ARG ...]", 2)
    if any("\0" in arg for arg in arguments):
        raise LauncherError("Command arguments cannot contain NUL.", 2)
    # A single quoted native command keeps the wrapper's Proton-argv detector
    # from mistaking a nested game command for this network prefix's target.
    command = "exec -- " + shlex.join(arguments)
    return [str(config.wrapper), "-p", str(config.prefix), "--runner", str(config.runner),
            "--", "/bin/bash", "-c", command]


def run_native(config: Config, arguments: list[str]) -> None:
    argv = network_argv(config, arguments)
    env = network_environment(config)
    validate_network(config, env)
    os.execve(argv[0], argv, env)


def run_accelerator(config: Config, arguments: list[str]) -> None:
    env = network_environment(config)
    validate_network(config, env)
    if not arguments:
        if not config.uu_executable.is_file():
            raise LauncherError("UU executable is missing; install the client in this prefix or set UU_EXE.")
        arguments = [str(config.uu_executable)]
    if any("\0" in arg for arg in arguments):
        raise LauncherError("Command arguments cannot contain NUL.", 2)
    for key, default in (("PROTON_USE_XALIA", "0"), ("PROTON_USE_WOW64", "1"), ("PROTON_NO_FSYNC", "1")):
        env.setdefault(key, default)
    if config.uu_locale is not None:
        env["LANG"] = env["LC_ALL"] = config.uu_locale
    if config.desktop_wrapper is not None:
        executable(config.desktop_wrapper, "UU_DESKTOP_WRAPPER")
        env["PROTON_DESKTOP_WRAPPER"] = str(config.desktop_wrapper)
    argv = [str(config.wrapper), "-p", str(config.prefix), "--runner", str(config.runner), "--", *arguments]
    os.execve(argv[0], argv, env)


class SteamProcess(NamedTuple):
    pid: int
    netns: str


def find_steam_processes(proc: Path = Path("/proc"), uid: int | None = None) -> list[SteamProcess]:
    """Inspect native same-user clients, excluding interpreter-based gates."""
    uid = os.getuid() if uid is None else uid
    result = []
    try:
        entries = list(proc.iterdir())
    except OSError as error:
        raise LauncherError("Cannot inspect existing Steam processes.") from error
    for entry in entries:
        if not entry.name.isascii() or not entry.name.isdecimal() or int(entry.name) == os.getpid():
            continue
        try:
            if entry.stat().st_uid != uid or (entry / "comm").read_text().strip() != "steam":
                continue
            image = os.readlink(entry / "exe").removesuffix(" (deleted)")
            if Path(image).name != "steam":
                continue
            namespace = os.readlink(entry / "ns/net")
            if not NETNS.fullmatch(namespace):
                raise LauncherError("Cannot identify an existing Steam process's network namespace.")
            result.append(SteamProcess(int(entry.name), namespace))
        except (FileNotFoundError, ProcessLookupError):
            continue
        except OSError as error:
            raise LauncherError("Cannot read an existing Steam process's network namespace.") from error
    return sorted(result)


def target_namespace(config: Config, env: dict[str, str]) -> str:
    argv = network_argv(config, ["/usr/bin/readlink", "/proc/self/ns/net"])
    try:
        result = subprocess.run(argv, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                                text=True, timeout=PROBE_TIMEOUT, check=False)
    except subprocess.TimeoutExpired as error:
        raise LauncherError("Checking the UU network timed out; retry after the wrapper session is ready.") from error
    except OSError as error:
        raise LauncherError("Cannot check the UU network session.") from error
    namespace = result.stdout.strip()
    if result.returncode != 0 or not NETNS.fullmatch(namespace):
        raise LauncherError("The wrapper did not confirm a network namespace; Steam was not launched.")
    return namespace


def run_steam(config: Config, arguments: list[str]) -> None:
    executable(config.steam_real, "STEAM_REAL")
    env = network_environment(config)
    validate_network(config, env)
    if find_steam_processes():
        wanted = target_namespace(config, env)
        if any(process.netns != wanted for process in find_steam_processes()):
            raise LauncherError("An existing Steam client uses a different network. Exit Steam normally, then use uu-network-steam. Nothing was closed automatically.", 2)
    argv = network_argv(config, [str(config.steam_real), *arguments])
    os.execve(argv[0], argv, env)


def notify_error(message: str, steam: bool = False) -> None:
    print(f"UU network: {message}", file=sys.stderr)
    if not steam:
        return
    try:
        subprocess.run(["notify-send", "UU network", message], stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3, check=False)
    except (OSError, subprocess.TimeoutExpired):
        pass


def main(mode: str, arguments: list[str] | None = None) -> int:
    arguments = sys.argv[1:] if arguments is None else arguments
    if mode not in ("launch", "run", "steam"):
        raise ValueError("Unknown entrypoint")
    try:
        if mode == "run" and (not arguments or not arguments[0]):
            raise LauncherError("Usage: uu-network-run COMMAND [ARG ...]", 2)
        config = load_config()
        {"launch": run_accelerator, "run": run_native, "steam": run_steam}[mode](config, arguments)
    except LauncherError as error:
        notify_error(str(error), steam=mode == "steam")
        return error.exit_code
    except OSError:
        notify_error("Cannot execute a configured launcher; check its dependencies and permissions.", steam=mode == "steam")
        return 1
    return 0
