"Module extension for vendored deps without MODULE.bazel (fmt, loguru, spdlog)."

load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")

def _vendor_deps_impl(mctx):
    # Derive the module root (ThirdParty/Logging/) from a known exported file.
    fmt_build = mctx.path(Label("//ThirdParty:fmt.BUILD"))
    module_root = fmt_build.dirname.dirname
    loguru_build = mctx.path(Label("//ThirdParty:loguru.BUILD"))
    spdlog_build = mctx.path(Label("//ThirdParty:spdlog.BUILD"))

    new_local_repository(
        name = "fmt",
        path = str(module_root) + "/ThirdParty/fmt",
        build_file_content = mctx.read(fmt_build),
    )
    new_local_repository(
        name = "loguru",
        path = str(module_root) + "/ThirdParty/loguru",
        build_file_content = mctx.read(loguru_build),
    )
    new_local_repository(
        name = "spdlog",
        path = str(module_root) + "/ThirdParty/spdlog",
        build_file_content = mctx.read(spdlog_build),
    )

vendor_deps = module_extension(
    implementation = _vendor_deps_impl,
)
