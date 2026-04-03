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

export GCOV_PREFIX=/persistent/coverage
export GCOV_PREFIX_STRIP=3

mkdir /persistent/unit_tests

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

cd /persistent/unit_tests
cp -f /opt/tests/cc_test_qnx cc_test_qnx
chmod +x cc_test_qnx
