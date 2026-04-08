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
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@score_rules_imagefs//rules/qnx:ifs.bzl", "qnx_ifs")
load("//src:runfiles_manifest.bzl", "runfiles_manifest")

exports_files(
    [
        "x86_64_qnx8/run_qemu_shell.sh",
        "x86_64_qnx8/run_qemu.sh",
        "x86_64_qnx8/run_under_qnx.sh",
        "arm64_qnx8/run_qemu_shell.sh",
        "arm64_qnx8/run_qemu.sh",
        "arm64_qnx8/run_under_qnx.sh",
    ],
    visibility = ["//visibility:public"],
)

pkg_files(
    name = "startup_pkg",
    testonly = True,
    srcs = [
        "common/prepare_test.sh",
        "common/run_test.sh",
    ] + select({
        "@platforms//cpu:x86_64": [
            "x86_64_qnx8/startup.sh",
        ],
        "@platforms//cpu:aarch64": [
            "arm64_qnx8/startup.sh",
        ],
    }),
    attributes = pkg_attributes(mode = "0755"),
    prefix = "/proc/boot",
    tags = ["manual"],
)

pkg_files(
    name = "fs_virtio9p_pkg",
    testonly = True,
    srcs = [
        "//src/common/virtio9p:fs-virtio9p",
        "//src/common/virtio9p:mount_virtio9p",
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
    template = select({
        "@platforms//cpu:x86_64": "x86_64_qnx8/init.build.template",
        "@platforms//cpu:aarch64": "arm64_qnx8/init.build.template",
    }),
)

qnx_ifs(
    name = "init",
    testonly = True,
    srcs = [
        ":fs_virtio9p_pkg",
        ":startup_pkg",
    ],
    build_file = ":init_build_test",
    extra_build_files = select({
        "@platforms//cpu:x86_64": ["x86_64_qnx8/tools.build"],
        "@platforms//cpu:aarch64": ["arm64_qnx8/tools.build"],
    }),
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
    template = select({
        "@platforms//cpu:x86_64": "x86_64_qnx8/init.build.template",
        "@platforms//cpu:aarch64": "arm64_qnx8/init.build.template",
    }),
)

qnx_ifs(
    name = "init_shell",
    testonly = True,
    srcs = [
        ":fs_virtio9p_pkg",
        ":startup_pkg",
    ],
    build_file = ":init_build_shell",
    extra_build_files = select({
        "@platforms//cpu:x86_64": ["x86_64_qnx8/tools.build"],
        "@platforms//cpu:aarch64": ["arm64_qnx8/tools.build"],
    }),
    tags = ["manual"],
    target_compatible_with = [
        "@platforms//os:qnx",
    ],
    visibility = ["//visibility:public"],
)

runfiles_manifest(
    name = "run_under_qnx_manifest",
    testonly = True,
    tags = ["manual"],
    targets = [
        ":init",
    ],
)

sh_binary(
    name = "run_under_qnx",
    testonly = True,
    srcs = select({
        "@platforms//cpu:x86_64": ["x86_64_qnx8/run_under_qnx.sh"],
        "@platforms//cpu:aarch64": ["arm64_qnx8/run_under_qnx.sh"],
    }),
    data = [
        ":init",
        ":run_under_qnx_manifest",
    ],
    tags = ["manual"],
    visibility = ["//visibility:public"],
)
