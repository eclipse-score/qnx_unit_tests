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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_TYPES_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace virtio9p
{

/// Maximum message size negotiated during Tversion.
constexpr std::uint32_t kMaxMessageSize = 131072U;

/// 9P2000.L protocol version string.
constexpr const char* kProtocolVersion = "9P2000.L";

/// No-fid sentinel value.
constexpr std::uint32_t kNoFid = static_cast<std::uint32_t>(~0U);

/// No-tag sentinel value for Tversion/Rversion.
constexpr std::uint16_t kNoTag = static_cast<std::uint16_t>(~0U);

/// 9P2000.L message types.
enum class MessageType : std::uint8_t
{
    // clang-format off
    kTlerror    = 6,
    kRlerror    = 7,
    kTversion   = 100,
    kRversion   = 101,
    kTattach    = 104,
    kRattach    = 105,
    kTwalk      = 110,
    kRwalk      = 111,
    kTlopen     = 12,
    kRlopen     = 13,
    kTlcreate   = 14,
    kRlcreate   = 15,
    kTread      = 116,
    kRread      = 117,
    kTwrite     = 118,
    kRwrite     = 119,
    kTclunk     = 120,
    kRclunk     = 121,
    kTgetattr   = 24,
    kRgetattr   = 25,
    kTreaddir   = 40,
    kRreaddir   = 41,
    kTmkdir     = 72,
    kRmkdir     = 73,
    kTunlinkat  = 76,
    kRunlinkat  = 77,
    kTrenameat  = 74,
    kRrenameat  = 75,
    // clang-format on
};

/// Wire header: [4B size][1B type][2B tag]
constexpr std::uint32_t kHeaderSize = 7U;

/// QID: unique server-side file identifier — [1B type][4B version][8B path]
struct Qid
{
    std::uint8_t type{0U};
    std::uint32_t version{0U};
    std::uint64_t path{0U};
};

/// File attributes returned by Tgetattr/Rgetattr.
struct NinePStat
{
    std::uint64_t valid{0U};
    Qid qid{};
    std::uint32_t mode{0U};
    std::uint32_t uid{0U};
    std::uint32_t gid{0U};
    std::uint64_t nlink{0U};
    std::uint64_t rdev{0U};
    std::uint64_t size{0U};
    std::uint64_t blksize{0U};
    std::uint64_t blocks{0U};
    std::uint64_t atime_sec{0U};
    std::uint64_t atime_nsec{0U};
    std::uint64_t mtime_sec{0U};
    std::uint64_t mtime_nsec{0U};
    std::uint64_t ctime_sec{0U};
    std::uint64_t ctime_nsec{0U};
    std::uint64_t btime_sec{0U};
    std::uint64_t btime_nsec{0U};
    std::uint64_t gen{0U};
    std::uint64_t data_version{0U};
};

/// Getattr request mask — request all attributes.
constexpr std::uint64_t kGetattrBasic = 0x000007ffULL;

/// Linux open flag values for 9P2000.L Tlopen/Tlcreate.
/// These differ from QNX values and must be used on the wire.
constexpr std::uint32_t kLinuxOWronly = 0x0001U;
constexpr std::uint32_t kLinuxORdwr = 0x0002U;
constexpr std::uint32_t kLinuxOCreat = 0x0040U;
constexpr std::uint32_t kLinuxOTrunc = 0x0200U;
constexpr std::uint32_t kLinuxOAppend = 0x0400U;

/// Linux AT_REMOVEDIR flag for Tunlinkat.
constexpr std::uint32_t kLinuxAtRemovedir = 0x0200U;

/// Directory entry from Treaddir/Rreaddir.
struct DirEntry
{
    Qid qid{};
    std::uint64_t offset{0U};
    std::uint8_t type{0U};
    std::string name{};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_TYPES_H
