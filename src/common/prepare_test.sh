#!/bin/sh

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

# GCOV_PREFIX_STRIP=3 strips the /proc/self/cwd/ prefix that QCC embeds as the
# working directory in cross-compiled binaries. This leaves the relative path
# (e.g. bazel-out/k8-fastbuild/bin/test/_objs/...) which matches what Bazel's
# collect_cc_coverage.sh expects in $COVERAGE_DIR.
export GCOV_PREFIX=/persistent/coverage
export GCOV_PREFIX_STRIP=3

mkdir /persistent/tmp
mkdir /persistent/unit_tests

# TEST_TMPDIR must point to a filesystem that supports the full set of POSIX
# operations (see Bazel Test Encyclopedia, "Initial conditions").
export TEST_TMPDIR=/persistent/tmp

ROOT_DIR="/opt/tests/cc_test_qnx.runfiles/"

if [ -d "$ROOT_DIR" ]; then
    if [ -d "/opt/tests/cc_test_qnx.runfiles/_main" ]; then
        ROOT_DIR=/opt/tests/cc_test_qnx.runfiles/_main
    fi
    find ${ROOT_DIR} -maxdepth 1 -mindepth 1 -type d -exec cp -R '{}' /persistent/unit_tests/ \;
fi

export GTEST_FILTER="$(cat /opt/tests/cc_test_qnx_filters.txt)"
export GTEST_OUTPUT="xml:/persistent/test.xml"

cp -R /opt/tests/libs /persistent/unit_tests/
export LD_LIBRARY_PATH="/persistent/unit_tests/libs:${LD_LIBRARY_PATH}"

# Re-export environment variables forwarded from the host. run_under_qnx.sh
# writes them as a fragment of `export NAME='value'` lines: the shell in the
# IFS cannot iterate a file line by line (its read builtin consumes the whole
# file on the first call), so a read loop would only export the first variable.
if [ -f /opt/tests/cc_test_qnx_env.sh ]; then
    . /opt/tests/cc_test_qnx_env.sh
fi

cd /persistent/unit_tests
cp -f /opt/tests/cc_test_qnx cc_test_qnx
chmod +x cc_test_qnx
