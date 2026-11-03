# BUILD file for spdlog (header-only, external fmt) — vendored ThirdParty/spdlog submodule.
# Mirrors CMake: SPDLOG_FMT_EXTERNAL_HO + header-only use of @fmt.
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files(["LICENSE"])

cc_library(
    name = "spdlog",
    hdrs = glob(["include/spdlog/**/*.h"]),
    defines = [
        "SPDLOG_FMT_EXTERNAL_HO",
    ],
    includes = ["include"],
    deps = ["@fmt"],
)
