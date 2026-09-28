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

# The sh_test entrypoint is a symlink in the consuming package, so $0 points
# there, not at this script. Resolve it so SCRIPT_DIR reaches src/<arch>/ and
# ../common/qemu_common.sh finds the shared helpers.
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
. "${SCRIPT_DIR}/../common/qemu_common.sh"

IFS_IMAGE=$1
TEST_IMAGE=$2

trap qemu_cleanup_fsdev EXIT
qemu_setup_fsdev

# Share test image via the 9p host directory (mounted as /opt/tests in the VM)
tar xf "${TEST_IMAGE}" -C "${FSDEV_PATH}"

qemu_setup_accel aarch64

qemu-system-aarch64 \
                -smp 2 \
                -m 2G \
                ${ACCEL} \
                -nographic \
                -kernel "${IFS_IMAGE}" \
                -serial mon:stdio \
                -no-reboot \
                -object rng-random,filename=/dev/urandom,id=rng0 \
                -device virtio-rng-device,rng=rng0 \
                -device virtio-net-device,mac=52:54:00:0d:81:90 \
                -fsdev local,id=fsdev0,path="${FSDEV_PATH}",security_model=none \
                -device virtio-9p-device,fsdev=fsdev0,mount_tag=hostshare \
                2>&1 | sed -u 's/[^[:print:]]//g' | sed -u 's/\r//'

qemu_extract_results "${FSDEV_PATH}"
