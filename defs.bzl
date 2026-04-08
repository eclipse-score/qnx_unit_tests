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
"""Public API for score_qnx_unit_tests."""

load("@score_qnx_unit_tests//src:test_qnx.bzl", "test_qnx")

def cc_test_qnx(name, cc_test, excluded_tests_filter = None):
    """Compile and run a C++ QNX unit test in a QEMU microVM.

    Args:
      name: Test name
      cc_test: cc_test target
      excluded_tests_filter: list of tests to be excluded from execution.
    """
    test_qnx(name = name, test = cc_test, excluded_tests_filter = excluded_tests_filter)

def rust_test_qnx(name, rust_test):
    """Compile and run a Rust QNX unit test in a QEMU microVM.

    Args:
      name: Test name
      rust_test: rust_test target
    """
    test_qnx(name = name, test = rust_test)
