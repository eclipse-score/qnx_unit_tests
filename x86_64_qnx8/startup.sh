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

pci-server --config=/proc/boot/pci_server.cfg
waitfor /dev/pci

pipe
waitfor /dev/pipe

random
waitfor /dev/random

fsevmgr
waitfor /dev/fsnotify

devb-ram ram capacity=1 blk ramdisk=256m,cache=512k,vnode=256
waitfor /dev/ram0

mkqnx6fs -q /dev/ram0

mount -t qnx6 /dev/ram0 /persistent

# Mount host shared directory via virtio-9p
while ! mount_virtio9p -o transport=pci none /opt/tests; do
    echo "Failed to mount /opt/tests via 9p. Retrying..."
done

devb-ram ram capacity=1 blk ramdisk=10m,cache=512k,vnode=256
waitfor /dev/ram1

mkqnx6fs -q /dev/ram1

mount -t qnx6 -o noexec /dev/ram1 /tmp_discovery

io-sock -m phy -m pci -d vtnet_pci

waitfor /dev/socket
if_up -p -r 200 -m 10 vtnet0

ifconfig vtnet0 name eth0

ifconfig eth0 10.0.2.15 netmask 255.255.255.0 mtu 1504 up
if_up -l -r 200 -m 10 eth0

mqueue
waitfor /dev/mqueue
