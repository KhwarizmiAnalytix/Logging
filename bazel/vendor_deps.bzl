"""Expose submodules without recording checkout-specific paths in the lockfile."""

def _vendored_repository_impl(ctx):
    # Resolve labels at fetch time, after the extension lock has been replayed.
    root = ctx.path(ctx.attr.root_marker).dirname.dirname
    source = root.get_child("ThirdParty").get_child(ctx.attr.directory)
    if not source.is_dir:
        fail("Missing ThirdParty/%s; run git submodule update --init --recursive" % ctx.attr.directory)
    for child in source.readdir():
        if ctx.attr.build_file and child.basename in ["BUILD", "BUILD.bazel"]:
            continue
        ctx.symlink(child, child.basename)
        if ctx.os.name.startswith("windows") and not child.is_dir:
            ctx.watch(child)
    if ctx.attr.build_file:
        ctx.symlink(ctx.attr.build_file, "BUILD.bazel")
    ctx.file("REPO.bazel", "")

_vendored_repository = repository_rule(
    implementation = _vendored_repository_impl,
    attrs = {
        "root_marker": attr.label(mandatory = True),
        "directory": attr.string(mandatory = True),
        "build_file": attr.label(),
    },
    local = True,
)

def _vendor_deps_impl(_mctx):
    for name, directory, build_file in [
        ("fmt", "fmt", Label("//bazel:fmt.BUILD")),
        ("loguru", "loguru", Label("//bazel:loguru.BUILD")),
        ("spdlog", "spdlog", Label("//bazel:spdlog.BUILD")),
        ("com_google_googletest", "googletest", None),
        ("com_google_benchmark", "benchmark", None),
    ]:
        _vendored_repository(
            name = name,
            root_marker = Label("//bazel:logging.bzl"),
            directory = directory,
            build_file = build_file,
        )

vendor_deps = module_extension(implementation = _vendor_deps_impl)
