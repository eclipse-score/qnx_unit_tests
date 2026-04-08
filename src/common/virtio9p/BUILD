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
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

package(
    default_visibility = [
        "//:__subpackages__",
    ],
)

# --- Logging (slog2) ---

cc_library(
    name = "virtio9p_log",
    srcs = ["src/log/log.cpp"],
    hdrs = ["include/log/log.h"],
    includes = ["include"],
    target_compatible_with = ["@platforms//os:qnx"],
    deps = ["//third_party:libslog2"],
)

# --- Protocol library (9P2000.L message handling) ---

cc_library(
    name = "nine_p_protocol",
    srcs = [
        "src/protocol/fid_pool.cpp",
        "src/protocol/nine_p_message.cpp",
        "src/protocol/nine_p_session.cpp",
    ],
    hdrs = [
        "include/protocol/fid_pool.h",
        "include/protocol/nine_p_message.h",
        "include/protocol/nine_p_session.h",
        "include/protocol/nine_p_types.h",
        "include/transport/transport.h",
    ],
    includes = ["include"],
    target_compatible_with = ["@platforms//os:qnx"],
    deps = [":virtio9p_log"],
)

# --- Transport layer (QNX-only for actual device access) ---

cc_library(
    name = "virtio_transport",
    srcs = [
        "src/transport/mmio_transport.cpp",
        "src/transport/pci_transport.cpp",
        "src/transport/virtqueue.cpp",
    ],
    hdrs = [
        "include/transport/mmio_transport.h",
        "include/transport/pci_transport.h",
        "include/transport/transport.h",
        "include/transport/virtio_defs.h",
        "include/transport/virtqueue.h",
    ],
    includes = ["include"],
    target_compatible_with = ["@platforms//os:qnx"],
    deps = [
        ":nine_p_protocol",
        ":virtio9p_log",
        "//third_party:libpci",
    ],
)

# --- Resource manager binary (QNX-only) ---

cc_binary(
    name = "fs-virtio9p",
    srcs = [
        "include/resmgr/fs_virtio9p.h",
        "src/main.cpp",
        "src/resmgr/fs_virtio9p.cpp",
        "src/resmgr/parse_args.cpp",
    ],
    includes = ["include"],
    target_compatible_with = ["@platforms//os:qnx"],
    deps = [
        ":nine_p_protocol",
        ":virtio9p_log",
        ":virtio_transport",
    ],
)

# --- Mount helper script ---

exports_files(
    ["mount_virtio9p"],
)
