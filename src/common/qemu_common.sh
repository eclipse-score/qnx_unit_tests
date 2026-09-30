#!/bin/bash

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

# Shared QEMU launch helpers sourced by x86_64 and aarch64 test scripts.

QEMU_EXPECTED_VERSION="8.2.2"

# Verify the QEMU binary is on PATH; warn on version mismatch (not a hard
# failure, so older or newer QEMU is not locked out).
qemu_check() {
    local binary="$1"
    command -v "${binary}" >/dev/null 2>&1 || {
        echo "ERROR: ${binary} not found. Install: sudo apt-get install -y qemu-system" >&2
        exit 1
    }
    local version
    version="$("${binary}" --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)" || true
    if [ -n "${version}" ] && [ "${version}" != "${QEMU_EXPECTED_VERSION}" ]; then
        echo "WARNING: ${binary} ${version} detected, CI uses ${QEMU_EXPECTED_VERSION}" >&2
    fi
}

# Set ACCEL, QEMU_CPU, and DISABLE_KVM for the given guest architecture.
# KVM and host-vendor CPU matching only apply when the host can run that guest
# natively (host arch == guest arch); a cross-arch host is always TCG, so the
# host's own CPU vendor is irrelevant and a generic model is used instead.
# Usage: qemu_setup_accel <x86_64|aarch64>
qemu_setup_accel() {
    local guest_arch="$1"
    local host_arch
    host_arch="$(uname -m)"

    case "${guest_arch}" in
        x86_64)
            qemu_check qemu-system-x86_64
            if [[ "${host_arch}" == "x86_64" ]]; then
                DISABLE_KVM="${DISABLE_KVM:-0}"
                if [[ -e /dev/kvm && -r /dev/kvm ]] && [[ "${DISABLE_KVM}" == 0 ]]; then
                    # "host" (pass through the physical CPU) only works with
                    # KVM; the TCG fallback below needs an emulatable model.
                    case "$(grep -m1 '^vendor_id' /proc/cpuinfo 2>/dev/null)" in
                        *AuthenticAMD*) QEMU_CPU="${QEMU_CPU:-EPYC-Milan}" ;;
                        *GenuineIntel*) QEMU_CPU="${QEMU_CPU:-Icelake-Server}" ;;
                        *) QEMU_CPU="${QEMU_CPU:-host}" ;;
                    esac
                    echo "KVM supported! CPU model: ${QEMU_CPU}"
                    ACCEL="-enable-kvm -cpu ${QEMU_CPU}"
                else
                    [[ "${DISABLE_KVM}" != 0 ]] && echo "KVM explicitly disabled!"
                    QEMU_CPU="${QEMU_CPU:-max}"
                    echo "CPU model: ${QEMU_CPU}"
                    ACCEL="-cpu ${QEMU_CPU}"
                fi
            else
                QEMU_CPU="${QEMU_CPU:-max}"
                echo "Cross-arch emulation (host ${host_arch}), no KVM. CPU model: ${QEMU_CPU}"
                ACCEL="-cpu ${QEMU_CPU}"
            fi
            ;;
        aarch64)
            qemu_check qemu-system-aarch64
            QEMU_CPU="${QEMU_CPU:-max}"
            echo "CPU model: ${QEMU_CPU}"
            ACCEL="-machine virt -cpu ${QEMU_CPU}"
            ;;
        *)
            echo "ERROR: qemu_setup_accel: unknown guest arch '${guest_arch}'" >&2
            exit 1
            ;;
    esac
}

# Write vars named in QNX_FORWARD_ENV as an `export NAME='value'` fragment at
# <fsdev_path>/cc_test_qnx_env.sh, sourced by prepare_test.sh in the guest: the
# shell in the IFS cannot iterate a file line by line (its read builtin
# consumes the whole file on the first call), so a read loop would only ever
# export the first variable.
# Usage: qemu_write_forwarded_env <fsdev_path>
qemu_write_forwarded_env() {
    local fsdev_path="$1"
    local env_file="${fsdev_path}/cc_test_qnx_env.sh"
    # Expands to the four characters '\'' — closes the quote, escapes one,
    # reopens — to quote arbitrary values for the guest shell.
    local sq_escape="'\\''"

    # A caller-supplied fsdev_path may be reused across runs, so never let a
    # previous run's variables leak into this one.
    rm -f "${env_file}"
    if [[ -n "${QNX_FORWARD_ENV:-}" ]]; then
        : > "${env_file}"
        local var
        for var in ${QNX_FORWARD_ENV//,/ }; do
            if [[ -n "${!var+x}" ]]; then
                printf "export %s='%s'\n" "${var}" "${!var//\'/${sq_escape}}" >> "${env_file}"
            fi
        done
    fi
}

# Create or reuse the virtio-9p shared directory.
qemu_setup_fsdev() {
    if [[ -z "${FSDEV_PATH:-}" ]]; then
        FSDEV_PATH=$(mktemp -d)
        FSDEV_PATH_CREATED=1
    else
        # A caller-supplied FSDEV_PATH is reusable across runs, and may point
        # at a pre-existing directory the caller doesn't want wiped wholesale.
        # Clear only the entries a launcher actually stages here (test
        # results, forwarded env, copied binary/runfiles/libs, and a leftover
        # extra-args fragment from a run that had args), never the directory
        # itself.
        local entry
        for entry in test_results cc_test_qnx cc_test_qnx.runfiles \
                cc_test_qnx_filters.txt cc_test_qnx_extra_args.sh \
                cc_test_qnx_env.sh libs; do
            rm -rf "${FSDEV_PATH:?}/${entry}"
        done
    fi
    mkdir -p "${FSDEV_PATH}"
}

# Remove the virtio-9p shared directory if we created it.
qemu_cleanup_fsdev() {
    if [[ "${FSDEV_PATH_CREATED:-0}" == "1" ]]; then
        rm -rf "${FSDEV_PATH}"
    fi
}

# Extract test results from the virtio-9p share and exit with the test's code.
# Usage: qemu_extract_results <fsdev_path>
qemu_extract_results() {
    local fsdev_path="$1"
    if [ -f "${fsdev_path}/test_results/test.xml" ]; then
        cp "${fsdev_path}/test_results/test.xml" "${XML_OUTPUT_FILE}"
    fi
    if [ -f "${fsdev_path}/test_results/test_output.log" ]; then
        # Apply the same control-character/CR filtering as the live QEMU
        # pipelines below, so buffered (non-streamed) output can't dump raw
        # ANSI/control bytes into the Bazel log.
        sed 's/[^[:print:]]//g; s/\r//' "${fsdev_path}/test_results/test_output.log"
    fi
    if [ -f "${fsdev_path}/test_results/coverage.tar.gz" ]; then
        tar -xf "${fsdev_path}/test_results/coverage.tar.gz" --no-same-owner --no-same-permissions -C "${TEST_UNDECLARED_OUTPUTS_DIR}"
        if [ -n "${COVERAGE_DIR:-}" ]; then
            tar -xf "${fsdev_path}/test_results/coverage.tar.gz" --no-same-owner --no-same-permissions -C "${COVERAGE_DIR}"
        fi
    fi
    local rc
    if [ -f "${fsdev_path}/test_results/returncode.log" ]; then
        rc="$(cat "${fsdev_path}/test_results/returncode.log")"
    else
        echo "ERROR: Test return code log not found!" >&2
        rc=1
    fi
    # An empty or non-numeric returncode would otherwise make `exit` return 0,
    # silently masking test failures. Default to 1 so a corrupt log fails.
    exit "${rc:-1}"
}
