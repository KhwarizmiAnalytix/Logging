"""Regression checks for the public Bazel helper and failure handling."""

import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
from helpers import bazel


class BazelSetupTests(unittest.TestCase):
    def setUp(self):
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def build(self, args, root=None):
        with patch.object(bazel.shutil, "which", return_value="bazel"):
            return bazel.BazelBuild(args, root)

    def test_target_selection_does_not_change_defaults(self):
        a = self.build(["build.test"])
        b = self.build(["build.test", "--target", "//Testing/..."])
        self.assertEqual(a.settings, b.settings)
        self.assertEqual(a.flags, b.flags)
        self.assertEqual(a.settings["compiler"], "default")
        self.assertEqual(a.settings["mode"], "dbg")

    def test_named_and_legacy_options_combine(self):
        b = self.build(
            [
                "config.shared.glog.cxx23.nocxademangle.portablefloat.logfatal",
                "--target",
                "//:Logging",
                "--backend",
                "native",
            ]
        )
        self.assertEqual(b.settings["backend"], "native")
        for flag in [
            "build_shared_libs=true",
            "logging_cxx_standard=23",
            "logging_enable_cxa_demangle=false",
            "logging_enable_portable_float_format=true",
            "logging_default_exception_mode=log_fatal",
        ]:
            self.assertIn("--define=" + flag, b.flags)

    def test_checkout_basename_does_not_select_project(self):
        with tempfile.TemporaryDirectory(prefix="logging-copy-") as root:
            b = self.build(["build", "--backend", "spdlog"], root)
        self.assertIn("--define=logging_backend=spdlog", b.flags)

    def test_long_backend_shortcut(self):
        b = self.build(["build.test", "--backend.spdlog", "--target", "//Testing/..."])
        self.assertEqual(b.settings["backend"], "spdlog")

    def test_repeated_targets_preserve_case_and_paths(self):
        b = self.build(
            ["test", "--target", "//Testing/Cxx:LoggingCxxTests", "--target", "//..."]
        )
        self.assertEqual(b.targets, ["//Testing/Cxx:LoggingCxxTests", "//..."])

    def test_cxx23_selects_project_and_toolchain_options(self):
        b = self.build(["build", "--cxx-standard", "23"])
        self.assertIn("--define=logging_cxx_standard=23", b.flags)
        self.assertIn(
            "--cxxopt=" + ("/std:c++latest" if os.name == "nt" else "-std=c++23"),
            b.flags,
        )

    def test_invalid_or_unimplemented_options_are_rejected(self):
        for args in [
            ["build.typo"],
            ["build", "--unknown"],
            ["build.sanitizer.address"],
            ["build.stdformat.cxx17"],
            ["build", "--define=x=y"],
            [],
        ]:
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.build(args)

    def test_help_prints_once(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), self.assertRaises(SystemExit) as ex:
            bazel.dispatch(["--help"])
        self.assertEqual(ex.exception.code, 0)
        self.assertEqual(output.getvalue().count("usage:"), 1)

    def test_clean_and_config_never_kill_processes_or_expunge(self):
        b = self.build(["clean.config", "--target", "//:Logging"])
        with patch.object(
            bazel.subprocess, "run", return_value=Mock(returncode=0)
        ) as run:
            self.assertEqual(b.execute(), 0)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual([cmd[1] for cmd in commands], ["clean", "build"])
        self.assertIn("--nobuild", commands[1])
        self.assertFalse(
            any("taskkill" in cmd or "--expunge" in cmd for cmd in commands)
        )

    def test_output_root_applies_to_info_and_query(self):
        b = self.build(["config", "--batch", "--output-root", "/tmp/logging-test"])
        with patch.object(
            bazel.subprocess, "run", return_value=Mock(stdout="/tmp/output\n")
        ) as run:
            self.assertEqual(b.info("output_base"), "/tmp/output")
        command = run.call_args.args[0]
        self.assertIn(
            "--output_user_root=" + str(Path("/tmp/logging-test").resolve()), command
        )
        self.assertIn("--batch", command)
        self.assertIn("--lockfile_mode=error", command)

    def test_command_failure_is_propagated(self):
        with (
            patch.object(bazel.shutil, "which", return_value="bazel"),
            patch.object(
                bazel.subprocess,
                "run",
                side_effect=subprocess.CalledProcessError(4, "bazel"),
            ),
        ):
            self.assertEqual(bazel.dispatch(["test"]), 4)

    def test_empty_coverage_is_not_a_report(self):
        for text in ["", "SF:source.cpp\nend_of_record\n", "LF:0\n"]:
            self.assertFalse(bazel.valid_lcov(text))
        self.assertTrue(bazel.valid_lcov("SF:source.cpp\nDA:12,1\nend_of_record\n"))

    @unittest.skipIf(os.name == "nt", "Clang/GCC coverage is a POSIX workflow")
    def test_coverage_uses_same_pipeline_with_named_options(self):
        for args in [
            ["coverage"],
            ["coverage", "--compiler", "clang", "--target", "//Testing/..."],
        ]:
            b = self.build(args)
            with patch.object(b, "coverage") as coverage, patch.object(b, "run") as run:
                self.assertEqual(b.execute(), 0)
            coverage.assert_called_once()
            run.assert_not_called()

    def test_versioned_compiler_paths_preserve_parent_case(self):
        self.assertEqual(
            bazel.compiler_pair("/Some/Case/clang-18"),
            ("/Some/Case/clang-18", str(Path("/Some/Case/clang++-18"))),
        )

    @unittest.skipIf(os.name == "nt", "Clang/GCC coverage is a POSIX workflow")
    def test_llvm_profiles_are_saved_before_any_followup_bazel_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sandbox = root / "sandbox"
            sandbox.mkdir()
            execution = root / "exec"
            execution.mkdir()
            (execution / "test-binary").touch()
            b = self.build(["coverage", "--compiler", "clang"])
            tested = False

            def info(key):
                self.assertFalse(
                    tested, "Bazel must not recycle profiles before merging"
                )
                return str(
                    {
                        "output_base": root,
                        "output_path": root / "out",
                        "bazel-testlogs": root / "logs",
                        "execution_root": execution,
                    }[key]
                )

            def run(action, *args, **kwargs):
                nonlocal tested
                if action == "coverage":
                    tested = True
                    (sandbox / "test.profraw").write_bytes(b"profile")
                return Mock(stdout="test-binary\n")

            def tool(command, **kwargs):
                if command[0] == "llvm-profdata":
                    self.assertTrue(tested)
                    self.assertEqual(len(command[5:]), 1)
                    self.assertEqual(Path(command[5]).read_bytes(), b"profile")
                    self.assertNotEqual(Path(command[5]).parent, sandbox)
                return Mock(stdout="SF:source.cpp\nDA:1,1\nend_of_record\n")

            with (
                patch.object(b, "info", side_effect=info),
                patch.object(b, "run", side_effect=run),
                patch.object(b, "llvm_tool", side_effect=lambda name: name),
                patch.object(bazel.subprocess, "run", side_effect=tool),
                patch.object(bazel.shutil, "which", return_value=None),
            ):
                b.coverage()
            self.assertTrue(
                bazel.valid_lcov((root / "out/_coverage/logging.lcov").read_text())
            )


if __name__ == "__main__":
    unittest.main()
