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

slogger2
waitfor /dev/slog

pipe
waitfor /dev/pipe

random -l devr-virtio.so:mem=0xa003e00
waitfor /dev/random

fsevmgr
waitfor /dev/fsnotify

devb-ram ram capacity=1 blk ramdisk=256m,cache=512k,vnode=256
waitfor /dev/ram0

while ! mkqnx6fs -q /dev/ram0; do
    echo "Failed to create QNX6 filesystem on /dev/ram0. Retrying..."
done

while ! mount -t qnx6 /dev/ram0 /persistent; do
    echo "Failed to mount /dev/ram0 on /persistent. Retrying..."
done

# Mount host shared directory via virtio-9p (MMIO transport for aarch64)
while ! mount_virtio9p -o smem=0xa003a00,irq=77 none /opt/tests; do
    echo "Failed to mount /opt/tests via 9p. Retrying..."
done

devb-ram ram capacity=1 blk ramdisk=10m,cache=512k,vnode=256
waitfor /dev/ram1

mkqnx6fs -q /dev/ram1

mount -t qnx6 -o noexec /dev/ram1 /tmp_discovery

io-sock -m phy -m pci -m usb -m fdt -d vtnet_mmio

waitfor /dev/socket

if_up -p -r 500 -m 10 vtnet0

ifconfig vtnet0 name eth0

ifconfig eth0 10.0.2.15 netmask 255.255.255.0 mtu 1504 up
if_up -l -r 200 -m 10 eth0

mqueue
waitfor /dev/mqueue

devc-pty -n 32 &
pdebug 38080
