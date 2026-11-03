"""One configuration and execution path for Logging's Bazel setup commands."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

BACKENDS = ("native", "loguru", "glog", "spdlog")
ACTIONS = {"config", "build", "test", "coverage", "clean"}


def boolean(value):
    if value.lower() in ("on", "true", "1", "yes"):
        return True
    if value.lower() in ("off", "false", "0", "no"):
        return False
    raise argparse.ArgumentTypeError("expected on or off")


def argument_parser():
    parser = argparse.ArgumentParser(
        description="Build Logging with Bazel. Named options override dotted shortcuts.",
        epilog="Examples: build.test.release --backend native; config.shared --target //:Logging; coverage --compiler clang",
        allow_abbrev=False,
    )
    parser.add_argument(
        "--compiler",
        help="default, clang, gcc, msvc, clang-cl, or a versioned compiler/path",
    )
    parser.add_argument(
        "--target", action="append", help="Target pattern; repeat for multiple targets"
    )
    parser.add_argument("--backend", choices=BACKENDS, type=str.lower)
    parser.add_argument("--compilation-mode", choices=("fastbuild", "dbg", "opt"))
    parser.add_argument(
        "--test-output", choices=("errors", "all", "summary", "streamed")
    )
    parser.add_argument("--batch", action="store_true")
    parser.add_argument(
        "--output-root",
        help="Bazel output/cache directory, also used for generated convenience links",
    )
    parser.add_argument("--cxx-standard", choices=("17", "20", "23"))
    parser.add_argument("--linkage", choices=("static", "shared"))
    parser.add_argument("--cxa-demangle", type=boolean)
    parser.add_argument("--portable-float-format", type=boolean)
    parser.add_argument("--format-use-std", type=boolean)
    parser.add_argument("--exception-mode", choices=("throw", "log_fatal"))
    parser.add_argument(
        "--lockfile-mode", choices=("error", "update", "off"), default="error"
    )
    return parser


def parse_arguments(arguments):
    parser = argument_parser()
    normalized = []
    for argument in arguments:
        if argument.startswith("--backend."):
            normalized.extend(["--backend", argument.split(".", 1)[1]])
        else:
            normalized.append(argument)
    named, remaining = parser.parse_known_args(normalized)
    legacy = dict(
        compiler="default",
        mode="dbg",
        backend="loguru",
        standard="20",
        linkage="static",
        cxa_demangle=True,
        portable_float_format=False,
        format_use_std=False,
        exception_mode="throw",
        test_output="errors",
    )
    actions = set()
    tokens = []
    for argument in remaining:
        # Compiler paths can contain dots, spaces, and case-sensitive components.
        if re.search(r"[/\\]", argument):
            tokens.append(argument)
        else:
            tokens.extend(argument.lower().split("."))
    for token in tokens:
        if token in ACTIONS:
            actions.add(token)
        elif token in BACKENDS:
            legacy["backend"] = token
        elif token == "backend":
            continue
        elif token in ("debug", "release", "relwithdebinfo"):
            legacy["mode"] = {
                "debug": "dbg",
                "release": "opt",
                "relwithdebinfo": "opt",
            }[token]
            legacy["debug_symbols"] = token == "relwithdebinfo"
        elif token in ("cxx17", "cxx20", "cxx23"):
            legacy["standard"] = token[3:]
        elif token in ("shared", "static"):
            legacy["linkage"] = token
        elif token in ("nocxademangle", "no_cxademangle"):
            legacy["cxa_demangle"] = False
        elif token == "portablefloat":
            legacy["portable_float_format"] = True
        elif token == "stdformat":
            legacy["format_use_std"] = True
        elif token in ("logfatal", "throw"):
            legacy["exception_mode"] = "log_fatal" if token == "logfatal" else "throw"
        elif token == "vv":
            legacy["test_output"] = "all"
        elif token == "batch":
            named.batch = True
        elif re.fullmatch(
            r"(?:clang(?:-cl)?|gcc|g\+\+)(?:-\d+)?|msvc|default",
            Path(token).name.lower(),
        ):
            legacy["compiler"] = token
        else:
            parser.error(f"unsupported action or option {token!r}; see --help")
    for field, source in [("mode", "compilation_mode"), ("standard", "cxx_standard")]:
        value = getattr(named, source)
        if value is not None:
            legacy[field] = value
    for field in (
        "compiler",
        "backend",
        "linkage",
        "cxa_demangle",
        "portable_float_format",
        "format_use_std",
        "exception_mode",
        "test_output",
    ):
        value = getattr(named, field)
        if value is not None:
            legacy[field] = value
    if not actions:
        parser.error("specify config, build, test, coverage, or clean")
    if legacy["format_use_std"] and legacy["standard"] == "17":
        parser.error("stdformat requires C++20 or C++23")
    return named, legacy, actions


def compiler_pair(compiler):
    if compiler == "default":
        return None
    if compiler == "msvc":
        return "cl", "cl"
    name = Path(compiler).name.lower().removesuffix(".exe")
    if name == "clang-cl":
        return compiler, compiler
    if re.fullmatch(r"clang(?:-\d+)?", name):
        return compiler, str(
            Path(compiler).with_name(Path(compiler).name.replace("clang", "clang++", 1))
        )
    if re.fullmatch(r"(?:gcc|g\+\+)(?:-\d+)?", name):
        if "g++" in name:
            return (
                str(
                    Path(compiler).with_name(
                        Path(compiler).name.replace("g++", "gcc", 1)
                    )
                ),
                compiler,
            )
        return compiler, str(
            Path(compiler).with_name(Path(compiler).name.replace("gcc", "g++", 1))
        )
    raise ValueError(f"Unsupported compiler {compiler!r}")


def valid_lcov(text):
    return any(line.startswith("SF:") for line in text.splitlines()) and any(
        re.fullmatch(r"DA:\d+,\d+(?:,.*)?", line) for line in text.splitlines()
    )


class BazelBuild:
    def __init__(self, arguments, root=None):
        self.options, self.settings, self.actions = parse_arguments(arguments)
        self.root = Path(root or Path(__file__).resolve().parents[2]).resolve()
        executable = shutil.which("bazelisk") or shutil.which("bazel")
        if not executable:
            raise RuntimeError("Install bazelisk or bazel")
        self.prefix = [executable]
        if executable.lower().endswith((".cmd", ".bat")):
            self.prefix = ["cmd.exe", "/c", executable]
        if self.options.batch:
            self.prefix.append("--batch")
        self.flags = ["--lockfile_mode=" + self.options.lockfile_mode]
        if self.options.output_root:
            output = Path(self.options.output_root).resolve()
            self.prefix.append("--output_user_root=" + str(output))
            self.flags.append("--symlink_prefix=" + str(output / "links") + "/")
        s = self.settings
        self.flags.extend(["-c", s["mode"]])
        if s.get("debug_symbols"):
            self.flags.append("--copt=" + ("/Zi" if os.name == "nt" else "-g"))
        if "coverage" in self.actions:
            if os.name == "nt":
                raise ValueError(
                    "Coverage collection is supported with Clang/GCC on Linux and macOS"
                )
            if s["compiler"] == "default":
                s["compiler"] = "clang"
        pair = compiler_pair(s["compiler"])
        if pair:
            self.flags.extend(["--repo_env=CC=" + pair[0], "--repo_env=CXX=" + pair[1]])
        standard = ("/std:c++" if os.name == "nt" else "-std=c++") + s["standard"]
        if os.name == "nt" and s["standard"] == "23":
            standard = "/std:c++latest"
        self.flags.extend(["--cxxopt=" + standard, "--host_cxxopt=" + standard])
        for key, value in {
            "logging_backend": s["backend"],
            "logging_cxx_standard": s["standard"],
            "build_shared_libs": s["linkage"] == "shared",
            "logging_enable_cxa_demangle": s["cxa_demangle"],
            "logging_enable_portable_float_format": s["portable_float_format"],
            "logging_format_use_std": s["format_use_std"],
            "logging_default_exception_mode": s["exception_mode"],
        }.items():
            self.flags.append(f"--define={key}={str(value).lower()}")
        self.targets = self.options.target or ["//..."]

    def run(self, action, extra=(), targets=True, capture=False):
        command = (
            self.prefix
            + [action]
            + (self.flags if action != "clean" else [])
            + list(extra)
        )
        if targets:
            command.extend(["--", *self.targets])
        print("Running:", subprocess.list2cmdline(command), flush=True)
        return subprocess.run(
            command, cwd=self.root, check=True, text=True, capture_output=capture
        )

    def info(self, key):
        return self.run("info", [key], targets=False, capture=True).stdout.strip()

    def llvm_tool(self, name):
        compiler = compiler_pair(self.settings["compiler"])[0]
        resolved = shutil.which(compiler)
        if resolved:
            sibling = Path(resolved).resolve().with_name(name)
            if sibling.is_file():
                return str(sibling)
        if sys.platform == "darwin":
            result = subprocess.run(
                ["xcrun", "--find", name], check=True, text=True, capture_output=True
            )
            return result.stdout.strip()
        tool = shutil.which(name)
        if not tool:
            raise RuntimeError(f"{name} is required to collect Clang coverage")
        return tool

    def coverage(self):
        clang = "clang" in self.settings["compiler"].lower()
        extra = [
            "--combined_report=lcov",
            "--instrumentation_filter=^//",
            "--instrument_test_targets",
            "--cache_test_results=no",
            "--test_output=" + self.settings["test_output"],
        ]
        llvm_profdata = self.llvm_tool("llvm-profdata") if clang else None
        llvm_cov = self.llvm_tool("llvm-cov") if clang else None
        coverage_dir = Path(self.info("output_path")) / "_coverage"
        coverage_dir.mkdir(parents=True, exist_ok=True)
        (coverage_dir / ("logging.lcov" if clang else "_coverage_report.dat")).unlink(
            missing_ok=True
        )
        if clang:
            output_base = Path(self.info("output_base"))
            roots = [output_base / "sandbox", Path(self.info("bazel-testlogs"))]
            # cquery resolves the actual configured executables, including aliases.
            expression = 'kind("cc_test", set(%s))' % " ".join(self.targets)
            result = self.run(
                "cquery",
                [
                    "--collect_code_coverage",
                    "--instrumentation_filter=^//",
                    "--instrument_test_targets",
                    expression,
                    "--output=files",
                ],
                targets=False,
                capture=True,
            )
            execution_root = Path(self.info("execution_root"))
            binaries = [
                execution_root / line
                for line in result.stdout.splitlines()
                if line.strip()
            ]
            if not binaries:
                raise RuntimeError("Could not resolve coverage test executables")
            merged = coverage_dir / "logging.profdata"
            # Remove old generated profiles before running tests. Comparing their
            # mtimes to wall time can discard fresh files on coarse filesystems.
            for root in roots:
                if root.is_dir():
                    for profile in root.rglob("*.profraw"):
                        profile.unlink(missing_ok=True)
            self.run("coverage", extra)
            # Resolve paths before the tests and snapshot profiles immediately
            # afterwards. A subsequent Bazel invocation can recycle the stash.
            with tempfile.TemporaryDirectory(prefix="raw-", dir=coverage_dir) as raw:
                profiles = []
                seen = set()
                for root in roots:
                    if root.is_dir():
                        for profile in root.rglob("*.profraw"):
                            try:
                                resolved = profile.resolve()
                                if resolved in seen:
                                    continue
                                destination = Path(raw) / (
                                    str(len(profiles)) + ".profraw"
                                )
                                shutil.copyfile(profile, destination)
                                profiles.append(str(destination))
                                seen.add(resolved)
                            except FileNotFoundError:
                                continue
                if not profiles:
                    raise RuntimeError("Coverage tests produced no fresh LLVM profiles")
                subprocess.run(
                    [llvm_profdata, "merge", "-sparse", "-o", str(merged), *profiles],
                    check=True,
                )
            command = [
                llvm_cov,
                "export",
                "-format=lcov",
                "-instr-profile=" + str(merged),
                "-ignore-filename-regex=(/external/|/ThirdParty/|^/opt/|^/usr/|^/Applications/)",
            ]
            for binary in binaries:
                # cquery analyzes targets without building their outputs. Check
                # existence only after coverage has built and run the tests.
                if not binary.is_file():
                    raise RuntimeError(f"Coverage test executable is missing: {binary}")
                command.extend(["-object", str(binary)])
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            content = re.sub(
                r"^SF:.*?/execroot/[^/]+/",
                "SF:" + str(self.root) + "/",
                result.stdout,
                flags=re.MULTILINE,
            )
            report = coverage_dir / "logging.lcov"
        else:
            self.run("coverage", extra)
            report = coverage_dir / "_coverage_report.dat"
            content = report.read_text(encoding="utf-8") if report.is_file() else ""
        if not valid_lcov(content):
            raise RuntimeError("Coverage collection produced no usable LCOV records")
        report.write_text(content, encoding="utf-8")
        print("Coverage report:", report)
        genhtml = shutil.which("genhtml")
        if genhtml:
            subprocess.run(
                [
                    genhtml,
                    str(report),
                    "--output-directory",
                    str(coverage_dir / "html"),
                    "--ignore-errors",
                    "inconsistent,corrupt,unsupported",
                ],
                check=True,
            )

    def execute(self):
        if "clean" in self.actions:
            self.run("clean", targets=False)
        if "coverage" in self.actions:
            self.coverage()
        else:
            if "config" in self.actions and not self.actions & {"build", "test"}:
                self.run("build", ["--nobuild"])
            if "build" in self.actions:
                self.run("build")
            if "test" in self.actions:
                self.run("test", ["--test_output=" + self.settings["test_output"]])
        return 0


def dispatch(arguments, root=None):
    # Keep the existing explicit raw-command escape hatch, with normal failure propagation.
    if arguments[:1] == ["--"]:
        executable = shutil.which("bazelisk") or shutil.which("bazel")
        if not executable:
            print("Install bazelisk or bazel")
            return 1
        prefix = [executable]
        if executable.lower().endswith((".cmd", ".bat")):
            prefix = ["cmd.exe", "/c", executable]
        try:
            return subprocess.run(prefix + arguments[1:], check=True).returncode
        except subprocess.CalledProcessError as error:
            return error.returncode
    try:
        return BazelBuild(arguments, root).execute()
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr)
        return error.returncode
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Bazel setup failed: {error}")
        return 1
