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

# run_under_qnx.sh — Bazel --run_under wrapper for QNX 8 unit test execution (x86_64).
#
# When used via --run_under, Bazel invokes this script with the cross-compiled
# QNX test binary path (and any test args) as arguments. This script:
#   1. Shares the test binary + runfiles via a virtio-9p host directory
#   2. Launches QEMU with the QNX boot IFS + shared directory
#   3. Extracts test results and returns the test exit code

set -euo pipefail

# Resolve SCRIPT_DIR without following symlinks, so it stays in the runfiles tree
# where Bazel places data dependencies alongside the script.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Locate data dependencies from the sh_binary's runfiles
IFS_IMAGE="${SCRIPT_DIR}/init.ifs"

# The test binary and optional args are passed by --run_under
TEST_BINARY="$1"
shift
TEST_ARGS=("$@")

if [[ ! -f "${TEST_BINARY}" ]]; then
    echo "ERROR: Test binary not found: ${TEST_BINARY}" >&2
    exit 1
fi

# --- Prepare writable copies of shared images ---
cleanup() {
    if [[ "${FSDEV_PATH_CREATED:-0}" == "1" ]]; then
        rm -rf "${FSDEV_PATH}"
    fi
}
trap cleanup EXIT

# --- Prepare host shared directory for virtio-9p ---
if [[ -z "${FSDEV_PATH:-}" ]]; then
    FSDEV_PATH=$(mktemp -d)
    FSDEV_PATH_CREATED=1
fi

# Share test binary + runfiles via the 9p host directory
mkdir -p "${FSDEV_PATH}"

# Copy test binary
cp "${TEST_BINARY}" "${FSDEV_PATH}/cc_test_qnx"

# Write filter file: use GTEST_FILTER env var if set, otherwise empty filter
if [[ -n "${GTEST_FILTER:-}" ]]; then
    echo "${GTEST_FILTER}" > "${FSDEV_PATH}/cc_test_qnx_filters.txt"
else
    echo "-" > "${FSDEV_PATH}/cc_test_qnx_filters.txt"
fi

# Write test args so run_test.sh can forward them to the binary inside the VM
if [[ ${#TEST_ARGS[@]} -gt 0 ]]; then
    printf '%s\n' "${TEST_ARGS[@]}" > "${FSDEV_PATH}/cc_test_qnx_extra_args.txt"
fi

# Copy test runfiles from the merged runfiles tree (PWD), excluding run_under
# infrastructure files listed in the manifest and .so shared libraries.
RUN_UNDER_MANIFEST="${SCRIPT_DIR}/run_under_qnx_manifest.txt"
if [[ -f "${RUN_UNDER_MANIFEST}" ]]; then
    # Build sorted exclude list from the manifest + additional infrastructure files
    EXCLUDE_FILE=$(mktemp)
    cp "${RUN_UNDER_MANIFEST}" "${EXCLUDE_FILE}"
    echo "run_under_qnx_manifest.txt" >> "${EXCLUDE_FILE}"
    # Exclude the test binary itself (already copied separately as cc_test_qnx)
    echo "${TEST_BINARY#${PWD}/}" >> "${EXCLUDE_FILE}"
    # Exclude the run_under shell scripts themselves
    echo "**/run_under_qnx.sh" >> "${EXCLUDE_FILE}"
    echo "**/run_under_qnx" >> "${EXCLUDE_FILE}"
    sort -u -o "${EXCLUDE_FILE}" "${EXCLUDE_FILE}"

    # Enumerate all regular files in the runfiles tree (following symlinks),
    # excluding .so shared libraries and run_under shell scripts by name
    ALL_FILES=$(mktemp)
    find "${PWD}" -follow -type f \
        ! -name "*.so" \
        ! -name "run_under_qnx.sh" \
        ! -name "run_under_qnx" \
        -printf '%P\n' | sort > "${ALL_FILES}"

    # Compute set difference: files present in runfiles but NOT in the exclude list
    COPY_LIST=$(mktemp)
    comm -23 "${ALL_FILES}" "${EXCLUDE_FILE}" > "${COPY_LIST}"

    # Copy only the difference, preserving directory structure and resolving symlinks
    DEST="${FSDEV_PATH}/cc_test_qnx.runfiles"
    while IFS= read -r rel_path; do
        mkdir -p "${DEST}/$(dirname "${rel_path}")"
        cp -L "${PWD}/${rel_path}" "${DEST}/${rel_path}"
    done < "${COPY_LIST}"

    rm -f "${EXCLUDE_FILE}" "${ALL_FILES}" "${COPY_LIST}"

    # Collect .so shared libraries into libs/ directory for LD_LIBRARY_PATH
    SO_DEST="${FSDEV_PATH}/libs"
    mkdir -p "${SO_DEST}"
    find "${PWD}" -follow -type f \( -name "*.so" -o -name "*.so.*" \) -print0 | while IFS= read -r -d '' so_file; do
        cp -L "${so_file}" "${SO_DEST}/$(basename "${so_file}")"
    done
else
    echo "WARNING: run_under manifest not found at ${RUN_UNDER_MANIFEST}, skipping runfiles copy" >&2
fi

# --- Launch QEMU (x86_64, QNX 8) ---
QEMU_CPU="${QEMU_CPU:-host}"
DISABLE_KVM="${DISABLE_KVM:-0}"

if [[ -e /dev/kvm && -r /dev/kvm ]] && [[ "${DISABLE_KVM}" == 0 ]]; then
    echo "KVM supported!"
    ACCEL="-enable-kvm -cpu ${QEMU_CPU}"
else
    [[ "${DISABLE_KVM}" != 0 ]] && echo "KVM explicitly disabled!"
    ACCEL="-cpu ${QEMU_CPU}"
fi

NETWORK="-netdev user,id=net0 -device virtio-net-pci,netdev=net0"

qemu-system-x86_64 \
                -smp 2 \
                -m 2G \
                ${ACCEL} \
                -nographic \
                -kernel "${IFS_IMAGE}" \
                -serial mon:stdio \
                -no-reboot \
                -object rng-random,filename=/dev/urandom,id=rng0 \
                ${NETWORK} \
                -device virtio-rng-pci,rng=rng0 \
                -fsdev local,id=fsdev0,path="${FSDEV_PATH}",security_model=none \
                -device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare,addr=0x07 \
                2>&1 | sed -u 's/[^[:print:]]//g' | sed -u 's/\r//'

# --- Extract test results ---
if [ -f "${FSDEV_PATH}/test_results/test.xml" ]; then
    cp ${FSDEV_PATH}/test_results/test.xml ${XML_OUTPUT_FILE}
fi

if [ -f "${FSDEV_PATH}/test_results/coverage.tar.gz" ]; then
    tar -xf ${FSDEV_PATH}/test_results/coverage.tar.gz --no-same-owner --no-same-permissions -C "${TEST_UNDECLARED_OUTPUTS_DIR}"
fi

if [ -f "${FSDEV_PATH}/test_results/returncode.log" ]; then
    exit $(cat "${FSDEV_PATH}/test_results/returncode.log")
else
    echo "ERROR: Test return code log not found!" >&2
    exit 1
fi
