# =============================================================================
# fmt Library BUILD Configuration
# =============================================================================
# Modern formatting library (header-only mode)
# Equivalent to ThirdParty/fmt
# =============================================================================

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files(["LICENSE"])

cc_library(
    name = "fmt",
    hdrs = glob([
        "include/fmt/*",
    ]),
    defines = ["FMT_HEADER_ONLY=1"],
    includes = ["include"],
    textual_hdrs = glob([
        "include/fmt/*",
    ]),
)
