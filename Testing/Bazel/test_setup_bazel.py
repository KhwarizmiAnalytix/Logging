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
    def test_clang_coverage_exports_lcov_before_sandbox_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            coverage = root / "out/_coverage"
            coverage.mkdir(parents=True)
            combined = coverage / "_coverage_report.dat"
            report = coverage / "logging.lcov"
            combined.write_text("stale combined report")
            report.write_text("stale exported report")
            b = self.build(["coverage", "--compiler", "clang"], root)

            def run(action, extra):
                self.assertEqual(action, "coverage")
                self.assertFalse(combined.exists())
                self.assertFalse(report.exists())
                self.assertIn("--repo_env=BAZEL_USE_LLVM_NATIVE_COVERAGE=1", b.flags)
                self.assertIn("--repo_env=GCOV=/llvm/llvm-profdata", b.flags)
                self.assertIn("--repo_env=BAZEL_LLVM_COV=/llvm/llvm-cov", b.flags)
                self.assertIn("--copt=-fcoverage-compilation-dir=.", b.flags)
                self.assertIn("--experimental_use_llvm_covmap", extra)
                self.assertIn("--experimental_generate_llvm_lcov", extra)
                self.assertIn("--cache_test_results=no", extra)
                self.assertIn("--instrument_test_targets", extra)
                # Bazel retains the combined report even after deleting the
                # test sandbox and its raw profiles.
                combined.write_text(
                    "SF:/tmp/bazel/execroot/_main/src/logger.cpp\n"
                    "DA:1,1\nend_of_record\n"
                )

            with (
                patch.object(b, "info", return_value=str(root / "out")) as info,
                patch.object(b, "run", side_effect=run) as coverage_run,
                patch.object(b, "llvm_tool", side_effect=lambda name: "/llvm/" + name),
                patch.object(bazel.shutil, "which", return_value=None),
            ):
                b.coverage()
            info.assert_called_once_with("output_path")
            coverage_run.assert_called_once()
            self.assertEqual(
                report.read_text(),
                f"SF:{b.root}/src/logger.cpp\nDA:1,1\nend_of_record\n",
            )

    @unittest.skipIf(os.name == "nt", "Clang/GCC coverage is a POSIX workflow")
    def test_missing_or_empty_coverage_cannot_reuse_stale_reports(self):
        for compiler in ("clang", "gcc"):
            for content in (None, "", "SF:source.cpp\nend_of_record\n"):
                with self.subTest(compiler=compiler, content=content):
                    with tempfile.TemporaryDirectory() as directory:
                        root = Path(directory)
                        coverage = root / "_coverage"
                        coverage.mkdir()
                        combined = coverage / "_coverage_report.dat"
                        report = coverage / "logging.lcov"
                        stale = "SF:stale.cpp\nDA:1,1\nend_of_record\n"
                        combined.write_text(stale)
                        report.write_text(stale)
                        b = self.build(["coverage", "--compiler", compiler])

                        def run(*args):
                            if content is not None:
                                combined.write_text(content)

                        with (
                            patch.object(b, "info", return_value=str(root)),
                            patch.object(b, "run", side_effect=run),
                            patch.object(b, "llvm_tool", side_effect=lambda name: name),
                            self.assertRaisesRegex(RuntimeError, "no usable LCOV"),
                        ):
                            b.coverage()
                        if compiler == "clang":
                            self.assertFalse(report.exists())

    @unittest.skipIf(os.name == "nt", "Clang/GCC coverage is a POSIX workflow")
    def test_gcc_coverage_keeps_combined_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            combined = root / "_coverage/_coverage_report.dat"
            content = "SF:src/logger.cpp\nDA:1,1\nend_of_record\n"
            b = self.build(["coverage", "--compiler", "gcc"])

            def run(action, extra):
                self.assertEqual(action, "coverage")
                combined.write_text(content)

            with (
                patch.object(b, "info", return_value=str(root)),
                patch.object(b, "run", side_effect=run),
                patch.object(b, "llvm_tool") as llvm_tool,
                patch.object(bazel.shutil, "which", return_value=None),
            ):
                b.coverage()
            llvm_tool.assert_not_called()
            self.assertEqual(combined.read_text(), content)
            self.assertFalse((root / "_coverage/logging.lcov").exists())


if __name__ == "__main__":
    unittest.main()
