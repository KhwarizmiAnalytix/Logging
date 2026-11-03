"""Exercise installed consumers and lockfile relocation outside the source tree."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(command, **kwargs):
    print("Running:", subprocess.list2cmdline(list(map(str, command))), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def verify_package(package, work):
    original = work / "installation-original"
    original.mkdir()
    with tarfile.open(package) as archive:
        archive.extractall(original, filter="data")
    prefix = work / "installation-relocated"
    original.rename(prefix)
    consumer = work / "consumer"
    shutil.copytree(ROOT / "Examples/FindPackageExample", consumer)
    build = work / "consumer-build"
    run(
        [
            "cmake",
            "-S",
            consumer,
            "-B",
            build,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_PREFIX_PATH=" + str(prefix),
        ]
    )
    run(["cmake", "--build", build])
    environment = dict(os.environ)
    environment["PATH"] = str(prefix / "bin") + os.pathsep + environment.get("PATH", "")
    run(["ctest", "--test-dir", build, "--output-on-failure"], env=environment)


def verify_relocation(work, output_root=None):
    executable = shutil.which("bazelisk") or shutil.which("bazel")
    if not executable:
        raise RuntimeError("Bazel is required for the relocation check")
    a = work / "checkout-original"
    a.mkdir()
    for name in (
        "MODULE.bazel",
        "MODULE.bazel.lock",
        "BUILD.bazel",
        ".bazelversion",
        ".bazelrc",
        ".bazelignore",
    ):
        shutil.copyfile(ROOT / name, a / name)
    shutil.copytree(ROOT / "bazel", a / "bazel")
    # Only read vendor sources; do not change the submodules or their metadata.
    (a / "ThirdParty").symlink_to(ROOT / "ThirdParty", target_is_directory=True)
    cache = Path(output_root).resolve() if output_root else work / "bazel-output"
    for index in (0, 1):
        checkout = a if index == 0 else work / "checkout-relocated"
        if index:
            a.rename(checkout)
        run(
            [
                executable,
                "--batch",
                "--output_user_root=" + str(cache),
                "--output_base=" + str(work / ("output-base-" + str(index))),
                "query",
                "deps(//:Logging)",
                "--lockfile_mode=error",
            ],
            cwd=checkout,
        )
        if "directory" not in (checkout / "MODULE.bazel.lock").read_text(
            encoding="utf-8"
        ):
            raise RuntimeError("The vendored repository lock was not populated")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package", type=Path, help="Tarball produced by //:logging_pkg_tar"
    )
    parser.add_argument(
        "--relocation",
        action="store_true",
        help="Verify a locked dependency query from a relocated checkout (POSIX)",
    )
    parser.add_argument(
        "--output-root", help="Share Bazel's output/install/repository caches"
    )
    args = parser.parse_args()
    if not args.package and not args.relocation:
        parser.error("choose --package and/or --relocation")
    with tempfile.TemporaryDirectory(prefix="logging-integration-") as directory:
        work = Path(directory).resolve()
        if args.package:
            verify_package(args.package.resolve(), work)
        if args.relocation:
            verify_relocation(work, args.output_root)
    print("Bazel integration checks passed")


if __name__ == "__main__":
    main()
