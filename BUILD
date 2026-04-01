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
load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@score_rules_imagefs//rules/qnx:ifs.bzl", "qnx_ifs")
load("@score_tooling//:defs.bzl", "copyright_checker", "use_format_targets")

exports_files(
    [
        "x86_64_qnx8/run_qemu_shell.sh",
        "x86_64_qnx8/run_qemu.sh",
    ],
    visibility = ["//visibility:public"],
)

pkg_files(
    name = "startup_pkg",
    testonly = True,
    srcs = [
        "common/run_test.sh",
        "x86_64_qnx8/startup.sh",
    ],
    attributes = pkg_attributes(mode = "0755"),
    prefix = "/proc/boot",
    tags = ["manual"],
)

pkg_files(
    name = "fs_virtio9p_pkg",
    testonly = True,
    srcs = [
        "//common/virtio9p:fs-virtio9p",
        "//common/virtio9p:mount_virtio9p",
    ],
    attributes = pkg_attributes(mode = "0755"),
    prefix = "/proc/boot",
    tags = ["manual"],
)

expand_template(
    name = "init_build_test",
    out = "init_test.build",
    substitutions = {
        "{RUN_BINARY}": "run_test.sh",
    },
    tags = ["manual"],
    template = "x86_64_qnx8/init.build.template",
)

qnx_ifs(
    name = "init",
    testonly = True,
    srcs = [
        ":fs_virtio9p_pkg",
        ":startup_pkg",
    ],
    build_file = ":init_build_test",
    extra_build_files = [
        "x86_64_qnx8/tools.build",
    ],
    tags = ["manual"],
    target_compatible_with = [
        "@platforms//os:qnx",
    ],
    visibility = ["//visibility:public"],
)

expand_template(
    name = "init_build_shell",
    out = "init_shell.build",
    substitutions = {
        "{RUN_BINARY}": "[+session] /bin/sh &",
    },
    tags = ["manual"],
    template = "x86_64_qnx8/init.build.template",
)

qnx_ifs(
    name = "init_shell",
    testonly = True,
    srcs = [
        ":fs_virtio9p_pkg",
        ":startup_pkg",
    ],
    build_file = ":init_build_shell",
    extra_build_files = [
        "x86_64_qnx8/tools.build",
    ],
    tags = ["manual"],
    target_compatible_with = [
        "@platforms//os:qnx",
    ],
    visibility = ["//visibility:public"],
)

###############################################################################
# Formatting and tooling targets
###############################################################################
copyright_checker(
    name = "copyright",
    srcs = [
        "cc_test_qnx.bzl",
        "common",
        "examples",
        "rust_test_qnx.bzl",
        "test",
        "test_qnx.bzl",
        "third_party",
        "tools",
        "x86_64_qnx8",
        "//:BUILD",
        "//:MODULE.bazel",
    ],
    config = "@score_tooling//cr_checker/resources:config",
    template = "@score_tooling//cr_checker/resources:templates",
    visibility = ["//visibility:public"],
)

use_format_targets()
