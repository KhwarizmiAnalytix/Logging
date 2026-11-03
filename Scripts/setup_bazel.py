#!/usr/bin/env python3
"""Build, test, package, and collect coverage for Logging with Bazel."""

import sys

from helpers.bazel import dispatch


if __name__ == "__main__":
    sys.exit(dispatch(sys.argv[1:]))
