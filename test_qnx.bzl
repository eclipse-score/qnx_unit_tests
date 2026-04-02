# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************
"""Macro for compiling and running QNX unit tests in a QEMU microVM."""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_rust//rust:rust_common.bzl", "CrateInfo")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

def _get_test_and_data_impl(ctx):
    data_files = []
    src_files = ctx.attr.src[DefaultInfo].files.to_list()
    for file in ctx.attr.src[DefaultInfo].data_runfiles.files.to_list():
        if not (file.basename.endswith(".so") or ".so." in file.basename) and file not in src_files:
            data_files.append(file)

    # Rename binary to canonical name so the IFS image has the expected path.
    # The test runner script (run_test.sh) expects the binary at /opt/tests/cc_test_qnx.
    src_binary = ctx.attr.src[DefaultInfo].files.to_list()[0]
    renamed_binary = ctx.actions.declare_file(ctx.attr.name + "_out/cc_test_qnx")
    ctx.actions.symlink(output = renamed_binary, target_file = src_binary)

    return [DefaultInfo(files = depset([renamed_binary]), runfiles = ctx.runfiles(files = data_files))]

_get_test_and_data = rule(
    implementation = _get_test_and_data_impl,
    attrs = {
        "src": attr.label(providers = [[CcInfo], [CrateInfo]]),
    },
)

def _get_so_libs_impl(ctx):
    so_files = []
    for file in ctx.attr.src[DefaultInfo].data_runfiles.files.to_list():
        if file.basename.endswith(".so") or ".so." in file.basename:
            so_files.append(file)
    return [DefaultInfo(files = depset(so_files))]

_get_so_libs = rule(
    implementation = _get_so_libs_impl,
    attrs = {
        "src": attr.label(providers = [[CcInfo], [CrateInfo]]),
    },
)

def _test_qnx(name, test, excluded_tests_filter):
    """Shared implementation for compiling and running QNX unit tests.

    Args:
      name: Test name
      test: cc_test or rust_test target
      excluded_tests_filter: list of tests to be excluded from execution.
    """
    excluded_tests_filter = excluded_tests_filter if excluded_tests_filter else []

    excluded_tests_filter_str = "-"
    for test_filter in excluded_tests_filter:
        excluded_tests_filter_str = excluded_tests_filter_str + (test_filter + ":\\")

    native.genrule(
        name = "{}_excluded_tests_filter".format(name),
        cmd_bash = """
        echo {} > $(@)
        """.format(excluded_tests_filter_str),
        testonly = True,
        tags = ["manual"],
        outs = ["{}_excluded_tests_filter.txt".format(name)],
    )

    _get_test_and_data(
        name = "%s_test_and_data" % name,
        src = test,
        testonly = True,
        tags = ["manual"],
    )

    _get_so_libs(
        name = "%s_so_libs" % name,
        src = test,
        testonly = True,
        tags = ["manual"],
    )

    pkg_files(
        name = "%s_test_and_runfiles" % name,
        srcs = [
            ":{}_test_and_data".format(name),
        ],
        include_runfiles = True,
        testonly = True,
        tags = ["manual"],
        attributes = pkg_attributes(mode = "0755"),
    )

    pkg_files(
        name = "%s_filter_file" % name,
        srcs = [
            ":{}_excluded_tests_filter".format(name),
        ],
        renames = {
            ":{}_excluded_tests_filter".format(name): "cc_test_qnx_filters.txt",
        },
        testonly = True,
        tags = ["manual"],
        attributes = pkg_attributes(mode = "0644"),
    )

    pkg_files(
        name = "%s_so_libs_pkg" % name,
        srcs = [
            ":{}_so_libs".format(name),
        ],
        prefix = "libs",
        testonly = True,
        tags = ["manual"],
        attributes = pkg_attributes(mode = "0755"),
    )

    pkg_filegroup(
        name = "%s_pkg" % name,
        srcs = [
            ":%s_test_and_runfiles" % name,
            ":%s_filter_file" % name,
            ":%s_so_libs_pkg" % name,
        ],
        testonly = True,
        tags = ["manual"],
    )

    pkg_tar(
        name = "%s_pkg_tar" % name,
        srcs = [
            ":%s_pkg" % name,
        ],
        testonly = True,
        tags = ["manual"],
    )

    sh_test(
        name = name,
        srcs = select({
            "@platforms//cpu:x86_64": ["@score_qnx_unit_tests//:x86_64_qnx8/run_qemu.sh"],
            "@platforms//cpu:aarch64": ["@score_qnx_unit_tests//:arm64_qnx8/run_qemu.sh"],
        }),
        args = [
            "$(location @score_qnx_unit_tests//:init)",
            "$(location :%s_pkg_tar)" % name,
        ],
        data = [
            ":%s_pkg_tar" % name,
            "@score_qnx_unit_tests//:init",
        ],
        timeout = "moderate",
        size = "medium",
        target_compatible_with = [
            "@platforms//os:qnx",
        ],
        tags = [
            "cpu:2",
            "microvm_qnx_test",
        ],
        flaky = True,
    )

    sh_binary(
        name = "%s_shell" % name,
        srcs = select({
            "@platforms//cpu:x86_64": ["@score_qnx_unit_tests//:x86_64_qnx8/run_qemu_shell.sh"],
            "@platforms//cpu:aarch64": ["@score_qnx_unit_tests//:arm64_qnx8/run_qemu_shell.sh"],
        }),
        args = [
            "$(location @score_qnx_unit_tests//:init_shell)",
            "$(locations :%s_pkg_tar)" % name,
        ],
        data = [
            ":%s_pkg_tar" % name,
            "@score_qnx_unit_tests//:init_shell",
        ],
        testonly = True,
        target_compatible_with = [
            "@platforms//os:qnx",
        ],
        tags = ["manual"],
    )

def test_qnx(name, test, excluded_tests_filter = None):
    """Compile and run a QNX unit test in a QEMU microVM.

    Supports both cc_test and rust_test targets (any target providing
    CcInfo or CrateInfo).

    Args:
      name: Test name
      test: cc_test or rust_test target
      excluded_tests_filter: list of tests to be excluded from execution.
        Examples (gtest only):
        FooTest.Test1 - do not run Test1 from test suite FooTest
        FooTest.* - do not run any test from test suite FooTest
        *FooTest.* - runs all non FooTest tests.
    """
    _test_qnx(name, test, excluded_tests_filter)
