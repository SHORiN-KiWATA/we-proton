# SPDX-License-Identifier: BSD-3-Clause
"""Migrated singleton tests; all process trees and subprocess calls are fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uu_network as launcher


CONFIG = launcher.Config(Path("/fixture/prefix"), Path("/fixture/proton"),
                         Path("/fixture/wrapper"), Path("/fixture/steam"), Path("/fixture/client.exe"))


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.scan = self.enterContext(mock.patch.object(launcher, "find_steam_processes"))
        self.probe = self.enterContext(mock.patch.object(launcher, "target_namespace", return_value="net:[200]"))
        self.execute = self.enterContext(mock.patch.object(launcher.os, "execve"))
        self.notify = self.enterContext(mock.patch.object(launcher, "notify_error"))
        self.enterContext(mock.patch.object(launcher, "load_config", return_value=CONFIG))
        self.enterContext(mock.patch.object(launcher, "validate_network"))
        self.enterContext(mock.patch.object(launcher, "executable"))
        self.enterContext(mock.patch.object(launcher, "network_environment", return_value={"TEST": "only"}))

    def assert_forwarded(self, arguments):
        expected = launcher.network_argv(CONFIG, [str(CONFIG.steam_real), *arguments])
        self.execute.assert_called_once_with(str(CONFIG.wrapper), expected, {"TEST": "only"})
        self.notify.assert_not_called()

    def test_no_client_launches_without_probe(self):
        self.scan.return_value = []
        self.assertEqual(launcher.main("steam", []), 0)
        self.probe.assert_not_called()
        self.assert_forwarded([])

    def test_all_clients_in_correct_namespace_forward(self):
        self.scan.return_value = [launcher.SteamProcess(10, "net:[200]"), launcher.SteamProcess(20, "net:[200]")]
        self.assertEqual(launcher.main("steam", ["steam://open/friends"]), 0)
        self.assertEqual(self.scan.call_count, 2)
        self.assert_forwarded(["steam://open/friends"])

    def test_wrong_namespace_is_not_closed_or_forwarded(self):
        self.scan.return_value = [launcher.SteamProcess(10, "net:[100]")]
        self.assertEqual(launcher.main("steam", []), 2)
        self.execute.assert_not_called()
        self.notify.assert_called_once()

    def test_mixed_namespaces_rejected(self):
        self.scan.return_value = [launcher.SteamProcess(10, "net:[200]"), launcher.SteamProcess(20, "net:[100]")]
        self.assertEqual(launcher.main("steam", []), 2)
        self.execute.assert_not_called()

    def test_client_exits_during_probe(self):
        self.scan.side_effect = [[launcher.SteamProcess(10, "net:[100]")], []]
        self.assertEqual(launcher.main("steam", []), 0)
        self.assert_forwarded([])

    def test_wrong_client_appears_during_probe(self):
        self.scan.side_effect = [[launcher.SteamProcess(10, "net:[200]")], [launcher.SteamProcess(20, "net:[100]")]]
        self.assertEqual(launcher.main("steam", []), 2)
        self.execute.assert_not_called()

    def test_probe_failure_cannot_forward(self):
        self.scan.return_value = [launcher.SteamProcess(10, "net:[200]")]
        self.probe.side_effect = launcher.LauncherError("probe failed")
        self.assertEqual(launcher.main("steam", []), 1)
        self.execute.assert_not_called()
        self.notify.assert_called_once_with("probe failed", steam=True)

    def test_special_uri_and_options_remain_verbatim(self):
        self.scan.return_value = [launcher.SteamProcess(10, "net:[200]")]
        args = ["steam://openurl/https://example.invalid/a%20b?x='\";$()&y=中文", "--", "a b", "a\\b", "line\nnext"]
        original = list(args)
        self.assertEqual(launcher.main("steam", args), 0)
        self.assert_forwarded(original)
        self.assertEqual(args, original)

    def test_exec_failure_reports_only_generic_error(self):
        self.scan.return_value = []
        self.execute.side_effect = FileNotFoundError()
        self.assertEqual(launcher.main("steam", ["steam://sensitive-fixture"]), 1)
        self.assertNotIn("sensitive-fixture", self.notify.call_args.args[0])

    def test_disabled_network_stops_before_scanning_or_probing(self):
        with mock.patch.object(launcher, "validate_network", side_effect=launcher.LauncherError("network disabled")):
            self.assertEqual(launcher.main("steam", []), 1)
        self.scan.assert_not_called()
        self.probe.assert_not_called()
        self.execute.assert_not_called()


class ProcTests(unittest.TestCase):
    def setUp(self):
        self.proc = Path(self.enterContext(tempfile.TemporaryDirectory()))

    def process(self, pid, comm="steam", ns="net:[200]", exe="/client/ubuntu12_32/steam"):
        path = self.proc / str(pid)
        (path / "ns").mkdir(parents=True)
        (path / "comm").write_text(comm + "\n")
        if exe is not None:
            (path / "exe").symlink_to(exe)
        if ns is not None:
            (path / "ns/net").symlink_to(ns)
        return path

    def test_exact_comm_and_same_uid(self):
        self.process(20)
        self.process(10)
        self.process(30, "steamwebhelper")
        self.process(40, "steam.sh")
        self.assertEqual(launcher.find_steam_processes(self.proc), [launcher.SteamProcess(10, "net:[200]"), launcher.SteamProcess(20, "net:[200]")])
        self.assertEqual(launcher.find_steam_processes(self.proc, uid=launcher.os.getuid()+1), [])

    def test_stale_pid_missing_namespace_ignored(self):
        self.process(10, ns=None)
        (self.proc / "20").mkdir()
        self.assertEqual(launcher.find_steam_processes(self.proc), [])

    def test_self_and_concurrent_interpreter_gates_excluded(self):
        self.process(launcher.os.getpid(), ns="net:[100]", exe="/usr/bin/python3")
        self.process(10, ns="net:[100]", exe="/usr/bin/python3")
        self.process(20, ns="net:[100]", exe="/usr/bin/bash")
        self.assertEqual(launcher.find_steam_processes(self.proc), [])

    def test_self_excluded_before_reads(self):
        self.process(launcher.os.getpid(), exe="/client/steam")
        with mock.patch.object(launcher.os, "readlink", side_effect=AssertionError("self read")):
            self.assertEqual(launcher.find_steam_processes(self.proc), [])

    def test_native_and_deleted_native_executable_namespaces(self):
        self.process(10, ns="net:[100]")
        self.process(20, ns="net:[200]", exe="/client/steam (deleted)")
        self.process(30, ns="net:[300]", exe="/usr/bin/python3 (deleted)")
        self.process(40, ns="net:[400]", exe="/client/steam-helper")
        self.assertEqual(launcher.find_steam_processes(self.proc), [launcher.SteamProcess(10, "net:[100]"), launcher.SteamProcess(20, "net:[200]")])

    def test_process_disappearing_before_exe_read_ignored(self):
        self.process(10, exe=None)
        self.assertEqual(launcher.find_steam_processes(self.proc), [])

    def test_unreadable_executable_fails_closed(self):
        self.process(10)
        with mock.patch.object(launcher.os, "readlink", side_effect=PermissionError()):
            with self.assertRaises(launcher.LauncherError):
                launcher.find_steam_processes(self.proc)

    def test_unreadable_namespace_fails_closed(self):
        self.process(10)
        readlink = launcher.os.readlink
        def guarded(path):
            if Path(path).name == "net":
                raise PermissionError()
            return readlink(path)
        with mock.patch.object(launcher.os, "readlink", side_effect=guarded):
            with self.assertRaises(launcher.LauncherError):
                launcher.find_steam_processes(self.proc)

    def test_malformed_namespace_rejected(self):
        self.process(10, ns="net:[200]\nextra")
        with self.assertRaises(launcher.LauncherError):
            launcher.find_steam_processes(self.proc)


class ProbeTests(unittest.TestCase):
    def test_valid_output_and_bounded_argv_probe(self):
        with mock.patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "net:[200]\n", "status")) as run:
            self.assertEqual(launcher.target_namespace(CONFIG, {}), "net:[200]")
            self.assertEqual(run.call_args.args[0], launcher.network_argv(CONFIG, ["/usr/bin/readlink", "/proc/self/ns/net"]))
            self.assertEqual(run.call_args.kwargs["timeout"], launcher.PROBE_TIMEOUT)
            self.assertFalse(run.call_args.kwargs.get("shell", False))

    def test_timeout_nonzero_and_malformed_output_fail_closed(self):
        outcomes = [subprocess.TimeoutExpired(["probe"], 8), FileNotFoundError(), subprocess.CompletedProcess([], 1, "net:[200]", ""), subprocess.CompletedProcess([], 0, "extra\nnet:[200]", "")]
        for outcome in outcomes:
            with self.subTest(outcome=type(outcome).__name__), mock.patch.object(launcher.subprocess, "run") as run:
                if isinstance(outcome, Exception):
                    run.side_effect = outcome
                else:
                    run.return_value = outcome
                with self.assertRaises(launcher.LauncherError):
                    launcher.target_namespace(CONFIG, {})

    def test_notification_failure_does_not_raise(self):
        with mock.patch.object(launcher.subprocess, "run", side_effect=subprocess.TimeoutExpired(["notify"], 3)) as run, mock.patch("builtins.print"):
            launcher.notify_error("test error", steam=True)
            self.assertEqual(run.call_args.kwargs["timeout"], 3)


if __name__ == "__main__":
    unittest.main()
