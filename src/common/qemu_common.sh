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

# Verify the QEMU binary is on PATH and log the detected version.
qemu_check() {
    local binary="$1"
    command -v "${binary}" >/dev/null 2>&1 || {
        echo "ERROR: ${binary} not found. Install: sudo apt-get install -y qemu-system" >&2
        exit 1
    }
    local version
    version="$("${binary}" --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)" || true
    if [ -n "${version}" ]; then
        echo "${binary} version ${version} detected"
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
# <fsdev_path>/cc_test_qnx_env.sh, sourced by prepare_test.sh in the guest.
# Snapshot selected values before introducing helper locals, because Bash uses
# dynamic scoping and locals can shadow caller variables of the same name.
qemu_write_forwarded_env() {
    local names="${QNX_FORWARD_ENV:-}"
    local values=()
    local requested_name
    for requested_name in ${names//,/ }; do
        if [[ -n "${!requested_name+x}" ]]; then
            values+=("${!requested_name}")
        else
            values+=("__QNX_FORWARD_ENV_UNSET__")
        fi
    done

    local fsdev_path="$1"
    local env_file="${fsdev_path}/cc_test_qnx_env.sh"
    local sq_escape="'\\''"
    rm -f "${env_file}"
    if [[ -n "${names}" ]]; then
        : > "${env_file}"
        local index=0 escaped_value
        for requested_name in ${names//,/ }; do
            if [[ "${values[index]}" != "__QNX_FORWARD_ENV_UNSET__" ]]; then
                escaped_value="$(printf '%s' "${values[index]}" | sed "s/'/'\\\\\\''/g")"
                printf "export %s='%s'\n" "${requested_name}" "${escaped_value}" >> "${env_file}"
            fi
            ((index += 1))
        done
    fi
}

# Create or reuse the virtio-9p shared directory.
qemu_setup_fsdev() {
    if [[ -z "${FSDEV_PATH:-}" ]]; then
        FSDEV_PATH="$(mktemp -d)"
        FSDEV_PATH_CREATED=1
    else
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
qemu_extract_results() {
    local fsdev_path="$1"
    if [ -f "${fsdev_path}/test_results/test.xml" ]; then
        cp "${fsdev_path}/test_results/test.xml" "${XML_OUTPUT_FILE}"
    fi
    if [ -f "${fsdev_path}/test_results/test_output.log" ]; then
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
    exit "${rc:-1}"
}
