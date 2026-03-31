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
load("@rules_cc//cc:defs.bzl", "cc_test")
load("//:cc_test_qnx.bzl", "cc_test_qnx")

cc_test(
    name = "main_cpp",
    srcs = ["main.cpp"],
    data = ["data.txt"],
    linkstatic = True,
    deps = [
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test_qnx(
    name = "main_cpp_qnx",
    cc_test = ":main_cpp",
)
