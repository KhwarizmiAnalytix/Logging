"""Documented setup actions and validated options shared by CI and local builds."""

import argparse
import contextlib
import io
import os
import re
import subprocess
from pathlib import Path
from typing import Optional

from .options import OPTIONS

ACTIONS = {"config", "build", "test", "install", "clean", "cleanbuild", "verifycache", "verifytoolchain"}
GENERATORS = ("default", "Ninja", "Ninja Multi-Config", "Xcode", "Visual Studio 16 2019", "Visual Studio 17 2022", "Visual Studio 18 2026")
COMPILERS = ("default", "msvc", "clang", "clang-cl", "gcc")


def canonical_build_type(value: str) -> str:
    """Return CMake's canonical configuration spelling."""
    return {"debug": "Debug", "release": "Release", "relwithdebinfo": "RelWithDebInfo", "minsizerel": "MinSizeRel"}.get(value.lower(), value)


def boolean(value: str) -> str:
    """Normalize documented Boolean values without accepting arbitrary definitions."""
    if value.lower() in ("on", "true", "1", "yes"):
        return "ON"
    if value.lower() in ("off", "false", "0", "no"):
        return "OFF"
    raise argparse.ArgumentTypeError("expected on or off")


def positive(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return number


def argument_parser(bazel: bool = False) -> argparse.ArgumentParser:
    """Build the public option registry used by parsing, help, and CI validation."""
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("--compiler", choices=COMPILERS, help="Compiler selection; default respects the build system/environment")
    parser.add_argument("--target", action="append", default=[], help="Build/test target; repeat for multiple targets")
    if bazel:
        from .bazel import argument_parser as bazel_parser

        return bazel_parser()
    parser.add_argument("--source-dir", help="Project or installed-package consumer source directory")
    parser.add_argument("--build-dir", help="Build directory; relative paths use the invoking directory")
    parser.add_argument("--build-type", type=canonical_build_type, choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"), help="Build and test configuration")
    parser.add_argument("--generator", choices=GENERATORS, help="Build generator; default respects CMAKE_GENERATOR")
    parser.add_argument("--c-compiler", help="C compiler executable")
    parser.add_argument("--cxx-compiler", help="C++ compiler executable")
    parser.add_argument("--architecture", help="Visual Studio target architecture")
    parser.add_argument("--preload-file", help="Initial project configuration file")
    parser.add_argument("--parallel", type=positive, default=4, help="Number of build jobs (default: 4)")
    parser.add_argument("--install-prefix", help="Installation prefix for config/install actions")
    parser.add_argument("--test-filter", help="Only run tests matching this regular expression")
    parser.add_argument("--test-label", help="Only run tests with a matching label")
    parser.add_argument("--test-timeout", type=positive, default=300, help="Per-test timeout in seconds (default: 300)")
    parser.add_argument("--memcheck", action="store_true", help="Run the CTest memory-check dashboard")
    parser.add_argument("--verbose", action="store_true", help="Show verbose build/test output")
    for option in OPTIONS:
        kind = option["kind"]
        converter = boolean if kind in ("bool", "cuda-compat") else (str.upper if kind == "upper-enum" else str.lower if kind == "enum" else str)
        aliases = ["--" + option["name"]]
        if "-" in option["name"]:
            aliases.append("--" + option["name"].replace("-", "_"))
        parser.add_argument(*aliases, dest=option["name"].replace("-", "_"), type=converter, choices=option.get("choices"), help=f"Set {option['name']} ({'on/off' if kind in ('bool','cuda-compat') else 'project setting'})")
    return parser


def split_cmake_args(arguments: list[str]) -> tuple[list[str], list[str]]:
    """Separate setup tokens from raw CMake arguments after ``--``."""
    if "--" not in arguments:
        return list(arguments), []
    index = arguments.index("--")
    return list(arguments[:index]), list(arguments[index + 1 :])


def apply_cmake_args(flags: list[str], overrides: list[str], build_type: str) -> str:
    """Append raw CMake definitions and honor an explicit build type."""
    flags.extend(overrides)
    prefix = "-DCMAKE_BUILD_TYPE="
    for override in overrides:
        if override.startswith(prefix):
            build_type = override[len(prefix) :]
    return build_type


def print_help() -> None:
    """Print the supported public actions and named options."""
    print("\nExact CI commands:")
    print("  python Scripts/setup.py cmake -S source -B build")
    print("  python Scripts/setup.py ctest --test-dir build")
    print("  python Scripts/setup.py config.build -- -DCMAKE_PREFIX_PATH=...")
    print("  python Scripts/setup.py verify-cache --build-dir build --target Logging")
    print(
        "  python Scripts/setup.py verify-toolchain --build-dir build --compiler clang"
    )
    print("\nSupported setup actions (combine with dots):")
    print("  config.build.test, install, cleanbuild, verifycache, verifytoolchain")
    print("  python Scripts/setup.py config.build.test.release.ninja.clang --build-dir build")
    print("  python Scripts/setup.py install.release --build-dir build --install-prefix install")
    print("\nOfficial named options:")
    print(argument_parser().format_help())


def print_bazel_help() -> None:
    print("\nSupported Bazel actions: config.build.test, clean, coverage")
    print("  python Scripts/setup_bazel.py build.test --target //Testing/... --compiler default")
    print(argument_parser(bazel=True).format_help())


def _reject_raw(arguments: list[str], parser: argparse.ArgumentParser) -> None:
    for argument in arguments:
        if argument in ("--", "cmake", "ctest", "bazel") or argument.startswith(("-D", "--cmake", "--copt", "--define", "--repo_env", "--test_output")):
            parser.error(f"unsupported argument {argument!r}; use documented setup actions and options")


def parse_options(arguments: list[str], bazel: bool = False):
    """Parse only declared options, retaining documented dotted legacy flags."""
    parser = argument_parser(bazel)
    _reject_raw(arguments, parser)
    options, remaining = parser.parse_known_args(arguments)
    legacy_prefixes = ("--backend.", "--gpu.", "--gpu_backend.", "--cpu_backend.", "--sanitizer.", "--lto.", "--linker.", "--packet-size=")
    for argument in remaining:
        if argument.startswith("-") and not argument.startswith(legacy_prefixes):
            parser.error(f"unrecognized option {argument!r}; see --help")
    return options, remaining


def validate_legacy(tokens, flags_factory, parser):
    """Reject misspelled actions rather than silently ignoring them."""
    flags = flags_factory([])
    allowed = set(next(value for name, value in vars(flags).items() if name.endswith("__key")))
    allowed.update(ACTIONS | {"release", "debug", "relwithdebinfo", "minsizerel", "ninja", "xcode", "vs17", "vs19", "vs22", "vs26", "clang", "clang-cl", "gcc", "msvc", "shared", "address", "undefined", "thread", "memory", "leak", "ccache", "sccache", "buildcache", "none", "no", "sse", "avx", "avx2", "avx512", "neon", "sve", "cuda", "hip", "metal", "native", "loguru", "glog", "spdlog", "sequential", "stdthread", "openmp", "tbb", "v", "vv", "torch", "noinstall", "tao", "logfatal", "throw", "nocxademangle", "nomimalloc", "batch", "clean"})
    for token in tokens:
        if token in allowed or token.startswith(("lto.", "linker.", "backend.", "gpu.", "gpu_backend.", "cpu_backend.", "parallel_backend.", "packet_size.")) or re.fullmatch(r"(?:cxx|c\+\+)\d+|psize\d+|sve(?:128|256|512)", token):
            continue
        if re.search(r"(?:clang|gcc|g\+\+)", token) and token not in ("cmake", "ctest"):
            continue
        parser.error(f"unrecognized action or flag {token!r}; see --help")


def _run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(command, check=True, **kwargs)


def _run_status(command: list[str]) -> int:
    """Run a command and return its status, including a non-zero exit."""
    try:
        result = subprocess.run(command, check=True)
        return result.returncode
    except subprocess.CalledProcessError as error:
        return error.returncode


def _option(arguments: list[str], name: str) -> Optional[str]:
    if name not in arguments:
        return None
    index = arguments.index(name)
    if index + 1 >= len(arguments):
        return None
    return arguments[index + 1]


def _dispatch_exact(arguments: list[str]) -> Optional[int]:
    """Run preserved cmake, ctest, and hyphenated verification commands."""
    if not arguments:
        return None
    if arguments[0] in ("cmake", "ctest"):
        return _run_status(arguments)
    if arguments[0] == "verify-cache":
        build = Path(_option(arguments, "--build-dir") or ".").resolve()
        target = _option(arguments, "--target") or ""
        parallel_text = _option(arguments, "--parallel") or "4"
        try:
            verify_cache(build, target, int(parallel_text))
        except subprocess.CalledProcessError as error:
            return error.returncode
        except (OSError, RuntimeError, ValueError) as error:
            print(error)
            return 1
        return 0
    if arguments[0] == "verify-toolchain":
        build = Path(_option(arguments, "--build-dir") or ".").resolve()
        compiler = _option(arguments, "--compiler") or "clang"
        generator = _option(arguments, "--generator") or "Ninja"
        try:
            verify_toolchain(build, compiler, generator)
        except (OSError, RuntimeError, ValueError) as error:
            print(error)
            return 1
        return 0
    return None


def _definitions(options) -> list[str]:
    definitions = []
    for option in OPTIONS:
        value = getattr(options, option["name"].replace("-", "_"))
        if value is None:
            continue
        if option["kind"] == "cuda-compat":
            value = "-allow-unsupported-compiler" if value == "ON" else ""
        if option["kind"] == "linker-flags":
            value = "" if value == "default" else "-fuse-ld=" + value
        definitions.append(f"-D{option['cache']}={value}")
    if options.install_prefix:
        definitions.append(f"-DCMAKE_INSTALL_PREFIX={options.install_prefix}")
    return definitions


def _explicit_legacy_definitions(tokens, flags_factory):
    """Translate explicit dotted feature choices without overriding project defaults."""
    with contextlib.redirect_stdout(io.StringIO()):
        baseline = flags_factory(["config"])
        selected = flags_factory(tokens or ["config"])
        baseline_flags, selected_flags = [], []
        baseline.create_cmake_flags(baseline_flags, "Release", os.name)
        selected.create_cmake_flags(selected_flags, "Release", os.name)
    defaults = dict(flag.split("=", 1) for flag in baseline_flags)
    return [flag for flag in selected_flags if defaults.get(flag.split("=", 1)[0]) != flag.split("=", 1)[1]]


def _configuration(build, requested):
    if requested:
        return requested
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.+)$", cache.read_text(encoding="utf-8"), re.M)
        if match:
            return canonical_build_type(match.group(1))
    return "Release"


def verify_cache(build: Path, target: str, parallel: int) -> None:
    """Require a real cache launcher and hits from a clean rebuild."""
    if not target:
        raise ValueError("verifycache requires --target")
    commands = _run(["cmake", "--build", str(build), "--target", target, "--", "-t", "commands"], capture_output=True, text=True).stdout
    (build / "compile-commands.txt").write_text(commands, encoding="utf-8")
    if not re.search(r'buildcache(?:\.exe)?"? .* (?:-c |/c |/Fo)', commands, re.I):
        raise RuntimeError("BuildCache is missing from the generated compiler commands")
    _run(["cmake", "--build", str(build), "--parallel", str(parallel)])
    _run(["cmake", "--build", str(build), "--clean-first", "--parallel", str(parallel)])
    statistics = _run(["buildcache", "--show-stats"], capture_output=True, text=True).stdout
    print(statistics, end="")
    (build / "cache-stats.txt").write_text(statistics, encoding="utf-8")
    if not re.search(r"(?:Direct|Local|Remote) hits: +[1-9]", statistics):
        raise RuntimeError("The clean rebuild did not produce any BuildCache hits")


def verify_toolchain(build: Path, expected: str, generator: str = "Ninja") -> None:
    """Verify the realized generator and compiler rather than trusting a matrix label."""
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if not re.search(rf"^CMAKE_GENERATOR:INTERNAL={re.escape(generator)}$", cache, re.M):
        raise RuntimeError(f"Expected the {generator} generator")
    files = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if len(files) != 1:
        raise RuntimeError("Expected one CMake C++ compiler description")
    compiler = files[0].read_text(encoding="utf-8")
    expected_ids = {"msvc": ("MSVC",), "clang": ("Clang", "AppleClang"), "clang-cl": ("Clang",), "gcc": ("GNU",)}.get(expected, ())
    if not any(f'set(CMAKE_CXX_COMPILER_ID "{expected_id}")' in compiler for expected_id in expected_ids):
        raise RuntimeError(f"CMake did not select the requested compiler {expected}")
    frontend = {"clang": "GNU", "clang-cl": "MSVC"}.get(expected)
    if frontend and f'set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "{frontend}")' not in compiler:
        raise RuntimeError(f"Expected the {frontend} frontend")
    print(f"Verified {expected} with {generator}")


def dispatch(
    arguments,
    root=None,
    flags_factory=None,
    configuration_factory=None,
    parse_legacy=None,
) -> Optional[int]:
    """Execute documented named options, leaving existing dotted usage compatible."""
    exact = _dispatch_exact(arguments)
    if exact is not None:
        return exact
    # Raw CMake arguments are only valid after `--`; keep them out of option parsing.
    arguments, overrides = split_cmake_args(arguments)
    if arguments == ["--help"]:
        return None
    if flags_factory is None or configuration_factory is None or parse_legacy is None:
        argument_parser().error("specify an action such as config.build.test")
    options, remaining = parse_options(arguments)
    tokens = parse_legacy(remaining)
    parser = argument_parser()
    validate_legacy(tokens, flags_factory, parser)
    modern = "msvc" in tokens or len(remaining) != len(arguments) or bool(set(tokens) & {"install", "cleanbuild", "clean", "verifycache", "verifytoolchain"})
    if not modern:
        return None
    phases = set(tokens) & ACTIONS
    if not phases:
        parser.error("specify an action such as config.build.test")
    root = Path(root).resolve()
    source = Path(options.source_dir).resolve() if options.source_dir else root
    build = Path(options.build_dir).resolve() if options.build_dir else root / "build_ninja"
    requested_type = options.build_type or next((canonical_build_type(token) for token in tokens if token in ("debug", "release", "relwithdebinfo", "minsizerel")), None)
    compiler = options.compiler or next((token for token in tokens if token in COMPILERS), "clang")
    generator = options.generator or ("Xcode" if "xcode" in tokens else "Ninja")
    definitions = _explicit_legacy_definitions(tokens, flags_factory) + _definitions(options)
    resolved_type = apply_cmake_args(definitions, overrides, requested_type or "")
    if resolved_type:
        requested_type = resolved_type
    try:
        if "config" in phases:
            command = ["cmake", "-S", str(source), "-B", str(build)]
            if generator != "default":
                command.extend(["-G", generator])
            if options.architecture:
                command.extend(["-A", options.architecture])
            if options.preload_file:
                command.extend(["-C", str(Path(options.preload_file).resolve())])
            if requested_type or not options.preload_file:
                command.append("-DCMAKE_BUILD_TYPE=" + (requested_type or "Release"))
            compilers = {"clang": ("clang", "clang++"), "gcc": ("gcc", "g++"), "clang-cl": ("clang-cl", "clang-cl"), "msvc": ("cl", "cl")}
            cc, cxx = compilers.get(compiler, (None, None))
            cc, cxx = options.c_compiler or cc, options.cxx_compiler or cxx
            if generator.startswith("Visual Studio") and compiler == "clang-cl":
                command.extend(["-T", "ClangCL"])
                cc, cxx = None, None
            if cc:
                command.append("-DCMAKE_C_COMPILER=" + cc)
            if cxx:
                command.append("-DCMAKE_CXX_COMPILER=" + cxx)
            _run(command + definitions)
        build_type = _configuration(build, requested_type)
        if "verifytoolchain" in phases:
            verify_toolchain(build, compiler, generator)
        if "clean" in phases:
            _run(["cmake", "--build", str(build), "--target", "clean", "--config", build_type])
        if "build" in phases or "cleanbuild" in phases:
            command = ["cmake", "--build", str(build), "--config", build_type, "--parallel", str(options.parallel)]
            if "cleanbuild" in phases:
                command.append("--clean-first")
            if options.target:
                command.extend(["--target", *options.target])
            if options.verbose:
                command.append("--verbose")
            _run(command)
        analysis = set(tokens) & {"cppcheck", "coverage", "spell", "benchmark"}
        configuration = None
        if analysis:
            configuration = configuration_factory(tokens)
            values = getattr(configuration, "_" + type(configuration).__name__ + "__value")
            values["build_enum"] = build_type
        original_dir = Path.cwd()
        try:
            if analysis:
                os.chdir(build)
            if "cppcheck" in analysis:
                result = configuration.cppcheck(str(source), str(build))
                if result:
                    return result
            if "verifycache" in phases:
                verify_cache(build, options.target[0] if options.target else None, options.parallel)
            if "test" in phases:
                command = ["ctest", "--test-dir", str(build), "-C", build_type, "--output-on-failure", "--no-tests=error", "--timeout", str(options.test_timeout)]
                if options.test_filter:
                    command.extend(["-R", options.test_filter])
                if options.test_label:
                    command.extend(["-L", options.test_label])
                if options.memcheck or "valgrind" in tokens:
                    command.extend(["-T", "memcheck"])
                if options.verbose:
                    command.append("-VV")
                _run(command)
            if "install" in phases:
                command = ["cmake", "--install", str(build), "--config", build_type]
                if options.install_prefix:
                    command.extend(["--prefix", options.install_prefix])
                _run(command)
            for action in ("coverage", "spell", "benchmark"):
                if action in analysis:
                    method = getattr(configuration, action, None)
                    if method is None:
                        continue
                    result = method(str(build)) if action == "benchmark" else method(str(source)) if action == "spell" else method(str(source), str(build))
                    if result:
                        return result
        finally:
            os.chdir(original_dir)
        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (OSError, RuntimeError, ValueError) as error:
        print(error)
        return 1


def dispatch_bazel(arguments, root=None, force=False) -> Optional[int]:
    """Compatibility entry point; all Bazel syntax uses the same dispatcher."""
    from .bazel import dispatch

    return dispatch(arguments, root)
