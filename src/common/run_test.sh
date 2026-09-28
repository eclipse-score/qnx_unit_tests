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

. /proc/boot/prepare_test.sh

# Bazel test args arrive as a fragment that sets the positional parameters.
# The shell in the IFS cannot iterate a file line by line: its read builtin
# consumes the whole file on the first call, so every later read hits EOF.
set --
if [ -f /opt/tests/cc_test_qnx_extra_args.sh ]; then
    . /opt/tests/cc_test_qnx_extra_args.sh
    echo "Test args: $*"
fi

# Test output written straight to the console goes through the emulated
# serial UART (one slow trap per character). Redirecting to the RAM disk
# (/persistent) is an order of magnitude faster for chatty tests. The host
# cats the captured log back to stdout after the run so --test_output still
# shows it. Set QEMU_STREAM_OUTPUT=1 to stream live instead (for interactive
# debugging at the cost of speed).
if [ "${QEMU_STREAM_OUTPUT:-0}" = "1" ]; then
    /persistent/unit_tests/cc_test_qnx "$@"
else
    /persistent/unit_tests/cc_test_qnx "$@" > /persistent/test_output.log 2>&1
fi

echo "$?" > /persistent/returncode.log

mkdir /opt/tests/test_results

cp -fR /persistent/returncode.log /opt/tests/test_results/returncode.log

if [ -e "/persistent/test.xml" ]; then
    cp -fR /persistent/test.xml /opt/tests/test_results/test.xml
fi

# Wait for all test processes to finish before snapshotting the log: a
# backgrounded child inheriting stdout/stderr can still be appending to it.
echo "Waiting for all test processes to finish..."
while pidin -F '%a %b %n' | grep cc_test_qnx > /dev/null 2>&1; do true; done
echo "Test processes finished"

if [ -e "/persistent/test_output.log" ]; then
    cp -fR /persistent/test_output.log /opt/tests/test_results/test_output.log
fi

if [ -d "/persistent/coverage" ]; then
    chmod -R 777 /persistent/coverage
    echo "Creating coverage archive..."
    time toybox tar -czf /opt/tests/test_results/coverage.tar.gz --owner=0 --group=0 -C /persistent/coverage .
    echo "Coverage archive created!"
fi

sync

cd /

umount /opt/tests

shutdown
