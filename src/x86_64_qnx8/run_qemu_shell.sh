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


NETWORK="-netdev user,id=net0 -device virtio-net-pci,netdev=net0"
if [ ! -z "${DEBUG_PORT}" ]; then
    echo "WARNING: Debugging enabled on port ${DEBUG_PORT}"
    NETWORK="-netdev user,id=net0,hostfwd=tcp:127.0.0.1:${DEBUG_PORT}-10.0.2.15:38080 -device virtio-net-pci,netdev=net0"
fi

QEMU_CPU="${QEMU_CPU:-host}"
DISABLE_KVM="${DISABLE_KVM:-0}"

if [[ -e /dev/kvm && -r /dev/kvm ]] && [[ "${DISABLE_KVM}" == 0 ]]; then
    echo "KVM supported!"
    ACCEL="-enable-kvm -cpu ${QEMU_CPU}"
else
    [[ "${DISABLE_KVM}" != 0 ]] && echo "KVM explicitly disabled!"
    ACCEL="-cpu ${QEMU_CPU}"
fi

qemu-system-x86_64 \
                -smp 2 \
                -m 2G \
                ${ACCEL} \
                -nographic \
                -kernel "${IFS_IMAGE}" \
                -serial mon:stdio \
                -object rng-random,filename=/dev/urandom,id=rng0 \
                -device virtio-rng-pci,rng=rng0 \
                ${NETWORK} \
                -fsdev local,id=fsdev0,path="${FSDEV_PATH}",security_model=none \
                -device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare,addr=0x07
