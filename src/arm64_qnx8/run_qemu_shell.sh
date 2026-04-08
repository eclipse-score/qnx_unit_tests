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

set -euo pipefail

IFS_IMAGE=$1
TEST_IMAGE=$2

DEBUG_PORT=""
if [ $# -eq 4 ]; then
    if [ "$3" == "--debug-port" ]; then
        DEBUG_PORT="$4"
    else
        echo "ERROR: Unknown argument '$3'"
        exit 1
    fi
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

# Share test image via the 9p host directory (mounted as /opt/tests in the VM)
tar xf "${TEST_IMAGE}" -C "${FSDEV_PATH}"

NETWORK="-device virtio-net-device,mac=52:54:00:0d:81:90"
if [ ! -z "${DEBUG_PORT}" ]; then
    echo "WARNING: Debugging enabled on port ${DEBUG_PORT}"
    NETWORK="-netdev user,id=net0,hostfwd=tcp:127.0.0.1:${DEBUG_PORT}-10.0.2.15:38080 -device virtio-net-device,netdev=net0,mac=52:54:00:0d:81:90"
fi

ACCEL="-machine virt -cpu max"

qemu-system-aarch64 \
                -smp 2 \
                -m 2G \
                ${ACCEL} \
                -nographic \
                -kernel "${IFS_IMAGE}" \
                -serial mon:stdio \
                -object rng-random,filename=/dev/urandom,id=rng0 \
                -device virtio-rng-device,rng=rng0 \
                ${NETWORK} \
                -fsdev local,id=fsdev0,path="${FSDEV_PATH}",security_model=none \
                -device virtio-9p-device,fsdev=fsdev0,mount_tag=hostshare
