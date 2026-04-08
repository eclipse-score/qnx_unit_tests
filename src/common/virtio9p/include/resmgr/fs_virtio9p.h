/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_RESMGR_FS_VIRTIO9P_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_RESMGR_FS_VIRTIO9P_H

#include "protocol/nine_p_session.h"
#include "transport/transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace virtio9p
{

/// Extended OCB (Open Control Block) that tracks per-open-file state.
/// Each client open() creates one of these, holding the 9P fid and offset.
struct Virtio9pOcb
{
    /// The 9P fid for this open file.
    std::uint32_t fid{0U};
    /// Current file offset for sequential reads.
    std::uint64_t offset{0U};
    /// Server-preferred I/O unit (from Rlopen).
    std::uint32_t iounit{0U};
    /// Whether this is a directory.
    bool is_directory{false};
    /// Linux open flags used when the file was opened.
    std::uint32_t open_flags{0U};
};

/// Configuration parsed from command-line arguments.
struct FsConfig
{
    /// Mount point in QNX namespace (e.g., "/mnt/host").
    std::string mount_point{"/mnt/host"};
    /// Transport type: "mmio" or "pci".
    std::string transport_type{};
    /// MMIO base address (ARM64 only).
    std::uint64_t mmio_base{0U};
    /// IRQ number (ARM64 only).
    std::uint32_t irq{0U};
    /// Whether to daemonize after resmgr_attach (fork, parent exits).
    bool daemonize{false};
};

/// Parse command-line arguments into FsConfig.
/// Usage: fs-virtio9p [-d] [-o smem=<addr>,irq=<n>,transport=mmio] <mountpoint>
/// @return 0 on success, -1 on error.
std::int32_t ParseArgs(int argc, char* argv[], FsConfig& config);

/// Create the appropriate transport based on config.
std::unique_ptr<Transport> CreateTransport(const FsConfig& config);

/// The main entry point: initialize transport, session, resmgr, and run.
/// @return Exit code (0 on clean shutdown).
std::int32_t FsVirtio9pMain(int argc, char* argv[]);

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_RESMGR_FS_VIRTIO9P_H
