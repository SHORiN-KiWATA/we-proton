# SPDX-License-Identifier: BSD-3-Clause
"""Isolated launcher tests; no real client, namespace or networking tool runs."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uu_network as launcher

KEYS = ("WINEPREFIX", "STEAM_COMPAT_DATA_PATH", "SteamAppId", "SteamGameId",
        "PROTON_NO_FSYNC", "PROTON_USE_WOW64", "PROTON_USE_XALIA",
        "PROTON_DESKTOP_WRAPPER", "PROTON_RUNNER", "PROTON_PREFIX", "PROTON_LANG",
        "LANG", "LC_ALL", "LC_CTYPE", "PATH")
REPORTER = '''#!/usr/bin/python3
import json, os, pathlib, sys
keys = %r
print(json.dumps({'executable': sys.argv[0], 'argv': sys.argv[1:],
                  'env': {key: os.environ.get(key) for key in keys}}))
if os.environ.get('TEST_RECORD_PATH'):
    pathlib.Path(os.environ['TEST_RECORD_PATH']).write_text('called')
raise SystemExit(int(os.environ.get('TEST_TARGET_STATUS', '0')))
''' % (KEYS,)
FAKE_WRAPPER = '''#!/usr/bin/python3
import json, os, pathlib, sys
pathlib.Path(os.environ['TEST_WRAPPER_RECORD']).write_text(json.dumps(sys.argv[1:]))
command = sys.argv[6:]
os.execv(command[0], command)
'''


class Fixture(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory(prefix="network test ")))
        self.prefix = self.root / "prefix 空格"
        self.prefix.mkdir()
        self.prefix_config = self.prefix / ".shorin-proton-wrapper.conf"
        self.prefix_config.write_text("netns=1\n")
        self.bin_dir = self.root / "tools"
        self.bin_dir.mkdir()
        self.wrapper = self.script("wrapper", FAKE_WRAPPER)
        self.runner = self.script("runtime", "#!/bin/sh\nexit 93\n")
        self.native = self.script("native 程序", REPORTER)
        self.pasta = self.script("pasta", "#!/bin/sh\nexit 94\n")
        self.config_file = self.root / "config.json"
        self.data = {"prefix": str(self.prefix), "runner": str(self.runner), "wrapper": str(self.wrapper),
                     "steam_real": str(self.native), "pasta_bin_dir": str(self.bin_dir), "uu_executable": str(self.native)}
        self.write_config()
        self.env = dict(os.environ)
        for key in list(self.env):
            if key in KEYS or key.startswith("UU_") or key in ("STEAM_REAL", "BASH_ENV"):
                del self.env[key]
        self.env.update(PATH="/usr/bin:/bin", LANG="C.UTF-8", PYTHONDONTWRITEBYTECODE="1", UU_NETWORK_CONFIG=str(self.config_file),
                        TEST_WRAPPER_RECORD=str(self.root / "wrapper-argv.json"))
        # HOME, when present, remains inherited unchanged. All test paths are explicit.
        self.assertEqual(self.env.get("HOME"), os.environ.get("HOME"))

    def script(self, name, source):
        path = self.bin_dir / name
        path.write_text(source)
        path.chmod(0o755)
        return path

    def write_config(self):
        self.config_file.write_text(json.dumps(self.data))

    def invoke(self, mode, args, extra=None):
        return subprocess.run([str(launcher.DIRECTORY / ("uu-network-" + mode)), *args],
                              env=self.env | (extra or {}), text=True, capture_output=True, timeout=5)


class NetworkRunTests(Fixture):
    def test_argv_preserved_and_shell_fragments_never_execute(self):
        sentinel = self.root / "must-not-exist"
        args = [str(self.native), "", "--", "a b", "quote'\"", "a\\b", "line\nnext",
                f"$(touch {sentinel})", f"`touch {sentinel}`", "*", "$HOME",
                "steam://openurl/https://example.invalid/?x=1&y=中文"]
        result = self.invoke("run", args)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["argv"], args[1:])
        self.assertEqual(report["executable"], args[0])
        recorded = json.loads((self.root / "wrapper-argv.json").read_text())
        self.assertEqual(recorded[:7], ["-p", str(self.prefix), "--runner", str(self.runner), "--", "/bin/bash", "-c"])
        self.assertEqual(len(recorded), 8)
        self.assertFalse(sentinel.exists())

    def test_absent_wine_options_stay_absent(self):
        result = self.invoke("run", [str(self.native)])
        self.assertEqual(result.returncode, 0, result.stderr)
        env = json.loads(result.stdout)["env"]
        for key in KEYS:
            if key not in ("PATH", "LANG"):
                self.assertIsNone(env[key], key)
        self.assertEqual(env["LANG"], self.env["LANG"])
        self.assertEqual(env["PATH"], str(self.bin_dir) + ":" + self.env["PATH"])

    def test_game_environment_and_locale_preserved(self):
        inherited = {"WINEPREFIX": "/games/specific/pfx", "STEAM_COMPAT_DATA_PATH": "/games/compatdata/42",
                     "SteamAppId": "42", "SteamGameId": "42", "PROTON_NO_FSYNC": "0", "PROTON_USE_WOW64": "0",
                     "PROTON_USE_XALIA": "1", "PROTON_DESKTOP_WRAPPER": "/chosen/wrapper", "PROTON_RUNNER": "/chosen/proton",
                     "PROTON_PREFIX": "/chosen/prefix", "PROTON_LANG": "", "LANG": "C", "LC_ALL": "C", "LC_CTYPE": "C"}
        result = self.invoke("run", [str(self.native)], inherited)
        self.assertEqual(result.returncode, 0, result.stderr)
        env = json.loads(result.stdout)["env"]
        for key, value in inherited.items():
            self.assertEqual(env[key], value, key)

    def test_missing_target_does_not_invoke_wrapper(self):
        result = self.invoke("run", [])
        self.assertEqual(result.returncode, 2)
        self.assertIn("Usage:", result.stderr)
        self.assertFalse((self.root / "wrapper-argv.json").exists())

    def test_target_failure_status_propagates(self):
        self.assertEqual(self.invoke("run", [str(self.native)], {"TEST_TARGET_STATUS": "37"}).returncode, 37)

    def test_nested_proton_tokens_are_hidden_from_wrapper_argv_scan(self):
        # Dependency contract test, not a claim to execute the upstream wrapper.
        args = ["--argument", "", "space value", "line\nnext", "/an/argument/proton", "waitforexitandrun", "/not-a-game.exe"]
        result = self.invoke("run", [str(self.native), *args])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["argv"], args)
        argv = json.loads((self.root / "wrapper-argv.json").read_text())
        self.assertEqual(argv[5:7], ["/bin/bash", "-c"])
        self.assertEqual(len(argv), 8)
        self.assertNotIn("waitforexitandrun", argv)
        self.assertNotIn("/an/argument/proton", argv)

    def test_disabled_or_missing_network_fails_without_wrapper(self):
        for value in (None, "netns=0\n", " netns=1\n", "netns=1\r\n"):
            with self.subTest(value=value):
                if value is None:
                    self.prefix_config.unlink(missing_ok=True)
                else:
                    self.prefix_config.write_bytes(value.encode())
                result = self.invoke("run", [str(self.native)])
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / "wrapper-argv.json").exists())

    def test_empty_command_is_usage_error(self):
        self.assertEqual(self.invoke("run", [""]).returncode, 2)

    def test_missing_pasta_is_a_clear_failure(self):
        self.pasta.unlink()
        result = self.invoke("run", [str(self.native)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pasta", result.stderr)
        self.assertFalse((self.root / "wrapper-argv.json").exists())


class ConfigTests(Fixture):
    def test_environment_overrides_config_and_defaults(self):
        cfg = launcher.load_config(self.env | {"UU_PREFIX": str(self.root / "another"), "STEAM_REAL": str(self.runner)})
        self.assertEqual(cfg.prefix, self.root / "another")
        self.assertEqual(cfg.steam_real, self.runner)
        self.assertEqual(cfg.wrapper, self.wrapper)

    def test_runner_is_required_without_file(self):
        env = {"XDG_CONFIG_HOME": str(self.root), "UU_WRAPPER": str(self.wrapper)}
        with self.assertRaisesRegex(launcher.LauncherError, "UU_RUNNER"):
            launcher.load_config(env)

    def test_environment_only_configuration(self):
        cfg = launcher.load_config({"XDG_CONFIG_HOME": str(self.root), "UU_RUNNER": str(self.runner), "UU_WRAPPER": str(self.wrapper)})
        self.assertEqual(cfg.steam_real, Path("/usr/bin/steam").resolve())
        self.assertIsNone(cfg.pasta_bin_dir)
        self.assertIsNone(cfg.uu_locale)
        self.assertIsNone(cfg.desktop_wrapper)

    def test_default_config_is_found_using_xdg(self):
        destination = self.root / "uu-network/config.json"
        destination.parent.mkdir()
        destination.write_text(json.dumps(self.data))
        cfg = launcher.load_config({"XDG_CONFIG_HOME": str(self.root)})
        self.assertEqual(cfg.runner, self.runner)

    def test_bad_json_types_unknown_fields_and_missing_explicit_file(self):
        for raw in ("not json", "[]", '{"unknown":"x"}', '{"runner":false}', '{"runner":null}'):
            with self.subTest(raw=raw):
                self.config_file.write_text(raw)
                with self.assertRaises(launcher.LauncherError):
                    launcher.load_config(self.env)
        self.config_file.unlink()
        with self.assertRaises(launcher.LauncherError):
            launcher.load_config(self.env)

    def test_relative_empty_and_control_paths_rejected(self):
        for value in ("relative/proton", "", "~/bad\npath", "/bad\0path", "~other/tool"):
            with self.subTest(value=value), self.assertRaises(launcher.LauncherError):
                launcher.load_config(self.env | {"UU_RUNNER": value})

    def test_tilde_expansion_does_not_change_home(self):
        before = os.environ.get("HOME")
        cfg = launcher.load_config(self.env | {"UU_PREFIX": "~/Games/test-expansion"})
        self.assertEqual(cfg.prefix, (Path.home() / "Games/test-expansion").resolve())
        self.assertEqual(os.environ.get("HOME"), before)

    def test_steam_real_recursion_and_symlink_rejected(self):
        alias = self.root / "custom-launcher"
        alias.symlink_to(launcher.ENTRYPOINTS[2])
        for path in (*launcher.ENTRYPOINTS, alias):
            with self.subTest(path=path), self.assertRaisesRegex(launcher.LauncherError, "recursion"):
                launcher.executable(path, "STEAM_REAL")

    def test_missing_or_nonexecutable_dependency_rejected(self):
        self.runner.chmod(0o644)
        with self.assertRaises(launcher.LauncherError):
            launcher.validate_network(launcher.load_config(self.env), self.env)

    def test_null_command_argument_rejected_without_exec(self):
        with self.assertRaises(launcher.LauncherError):
            launcher.network_argv(launcher.load_config(self.env), ["/bin/true", "bad\0arg"])


class AcceleratorTests(Fixture):
    def invoke_mocked(self, arguments, extra=None):
        with mock.patch.dict(os.environ, self.env | (extra or {}), clear=True), mock.patch.object(launcher.os, "execve") as execute:
            status = launcher.main("launch", arguments)
        return status, execute

    def test_defaults_are_uu_only_and_original_arguments_are_argv(self):
        arguments = ["--not-an-option-to-wrapper", "space value", "$(literal)"]
        status, execute = self.invoke_mocked(arguments)
        self.assertEqual(status, 0)
        argv, env = execute.call_args.args[1:]
        self.assertEqual(argv[5:], ["--", *arguments])
        self.assertEqual(env["PROTON_NO_FSYNC"], "1")
        self.assertEqual(env["PROTON_USE_WOW64"], "1")
        self.assertEqual(env["PROTON_USE_XALIA"], "0")
        self.assertEqual(env["LANG"], self.env["LANG"])
        self.assertNotIn("LC_ALL", env)
        self.assertNotIn("PROTON_DESKTOP_WRAPPER", env)

    def test_explicit_existing_proton_choices_are_honored(self):
        choices = {"PROTON_NO_FSYNC": "0", "PROTON_USE_WOW64": "0", "PROTON_USE_XALIA": "1"}
        status, execute = self.invoke_mocked([], choices)
        self.assertEqual(status, 0)
        env = execute.call_args.args[2]
        for key, value in choices.items():
            self.assertEqual(env[key], value)
        self.assertEqual(execute.call_args.args[1][-1], str(self.native))

    def test_optional_locale_and_desktop_wrapper(self):
        status, execute = self.invoke_mocked([], {"UU_LOCALE": "C.UTF-8", "UU_DESKTOP_WRAPPER": str(self.native)})
        self.assertEqual(status, 0)
        env = execute.call_args.args[2]
        self.assertEqual(env["LANG"], "C.UTF-8")
        self.assertEqual(env["LC_ALL"], "C.UTF-8")
        self.assertEqual(env["PROTON_DESKTOP_WRAPPER"], str(self.native))

    def test_missing_default_accelerator_does_not_exec(self):
        self.native.unlink()
        with mock.patch.object(launcher, "notify_error"):
            status, execute = self.invoke_mocked([])
        self.assertEqual(status, 1)
        execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
