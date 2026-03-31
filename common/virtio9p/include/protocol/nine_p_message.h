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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_MESSAGE_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_MESSAGE_H

#include "protocol/nine_p_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace virtio9p
{

/// Serializes and deserializes 9P2000.L wire-format messages.
///
/// Wire format: [4B size (little-endian)][1B type][2B tag][payload...]
/// All multi-byte integers are little-endian.
/// Strings are encoded as [2B length][UTF-8 bytes] (no NUL terminator).
class NinePMessage final
{
  public:
    /// Construct an empty message for building a request.
    NinePMessage() = default;

    /// Construct from a raw wire buffer (for parsing responses).
    explicit NinePMessage(std::vector<std::uint8_t> data);

    // --- Encoder helpers (build request payload) ---

    /// Start a new message of the given type and tag.
    void BeginMessage(MessageType type, std::uint16_t tag);

    /// Finalize the message: patches the 4-byte size header.
    void EndMessage();

    /// Append a uint8 to the payload.
    void PutU8(std::uint8_t value);

    /// Append a uint16 (little-endian) to the payload.
    void PutU16(std::uint16_t value);

    /// Append a uint32 (little-endian) to the payload.
    void PutU32(std::uint32_t value);

    /// Append a uint64 (little-endian) to the payload.
    void PutU64(std::uint64_t value);

    /// Append a 9P string: [2B length][bytes].
    void PutString(const std::string& value);

    /// Append raw bytes.
    void PutBytes(const std::uint8_t* data, std::uint32_t length);

    // --- Decoder helpers (parse response payload) ---

    /// Read a uint8 at the current offset and advance.
    std::uint8_t GetU8();

    /// Read a uint16 (little-endian) at the current offset and advance.
    std::uint16_t GetU16();

    /// Read a uint32 (little-endian) at the current offset and advance.
    std::uint32_t GetU32();

    /// Read a uint64 (little-endian) at the current offset and advance.
    std::uint64_t GetU64();

    /// Read a 9P string at the current offset and advance.
    std::string GetString();

    /// Read raw bytes at the current offset and advance.
    std::vector<std::uint8_t> GetBytes(std::uint32_t length);

    /// Read a QID from the current offset and advance.
    Qid GetQid();

    // --- Header accessors ---

    /// Parse the message type from the header.
    MessageType GetType() const;

    /// Parse the tag from the header.
    std::uint16_t GetTag() const;

    /// Parse the total size from the header.
    std::uint32_t GetSize() const;

    /// Reset the read offset to just past the header.
    void ResetReadOffset();

    /// Get the raw buffer.
    const std::vector<std::uint8_t>& GetData() const;

    /// Get the raw buffer (mutable, for transport layer).
    std::vector<std::uint8_t>& GetMutableData();

  private:
    std::vector<std::uint8_t> data_{};
    std::uint32_t read_offset_{kHeaderSize};
};

// --- High-level message builders ---

/// Build a Tversion request.
NinePMessage BuildTversion(std::uint32_t max_size, const std::string& version);

/// Build a Tattach request.
NinePMessage BuildTattach(std::uint16_t tag,
                          std::uint32_t fid,
                          std::uint32_t afid,
                          const std::string& uname,
                          const std::string& aname,
                          std::uint32_t n_uname);

/// Build a Twalk request.
NinePMessage BuildTwalk(std::uint16_t tag,
                        std::uint32_t fid,
                        std::uint32_t newfid,
                        const std::vector<std::string>& names);

/// Build a Tlopen request.
NinePMessage BuildTlopen(std::uint16_t tag, std::uint32_t fid, std::uint32_t flags);

/// Build a Tread request.
NinePMessage BuildTread(std::uint16_t tag, std::uint32_t fid, std::uint64_t offset, std::uint32_t count);

/// Build a Tclunk request.
NinePMessage BuildTclunk(std::uint16_t tag, std::uint32_t fid);

/// Build a Tgetattr request.
NinePMessage BuildTgetattr(std::uint16_t tag, std::uint32_t fid, std::uint64_t request_mask);

/// Build a Treaddir request.
NinePMessage BuildTreaddir(std::uint16_t tag, std::uint32_t fid, std::uint64_t offset, std::uint32_t count);

/// Build a Twrite request.
NinePMessage BuildTwrite(std::uint16_t tag,
                         std::uint32_t fid,
                         std::uint64_t offset,
                         const std::uint8_t* data,
                         std::uint32_t count);

/// Build a Tlcreate request.
NinePMessage BuildTlcreate(std::uint16_t tag,
                           std::uint32_t fid,
                           const std::string& name,
                           std::uint32_t flags,
                           std::uint32_t mode,
                           std::uint32_t gid);

/// Build a Tmkdir request.
NinePMessage BuildTmkdir(std::uint16_t tag,
                         std::uint32_t dfid,
                         const std::string& name,
                         std::uint32_t mode,
                         std::uint32_t gid);

/// Build a Tunlinkat request.
NinePMessage BuildTunlinkat(std::uint16_t tag, std::uint32_t dirfid, const std::string& name, std::uint32_t flags);

/// Build a Trenameat request.
NinePMessage BuildTrenameat(std::uint16_t tag,
                            std::uint32_t olddirfid,
                            const std::string& oldname,
                            std::uint32_t newdirfid,
                            const std::string& newname);

// --- Response parsers ---

/// Parse an Rversion response. Returns negotiated max_size and version string.
bool ParseRversion(NinePMessage& msg, std::uint32_t& max_size, std::string& version);

/// Parse an Rattach response.
bool ParseRattach(NinePMessage& msg, Qid& qid);

/// Parse an Rwalk response.
bool ParseRwalk(NinePMessage& msg, std::vector<Qid>& qids);

/// Parse an Rlopen response.
bool ParseRlopen(NinePMessage& msg, Qid& qid, std::uint32_t& iounit);

/// Parse an Rread response.
bool ParseRread(NinePMessage& msg, std::vector<std::uint8_t>& data);

/// Parse an Rclunk response.
bool ParseRclunk(NinePMessage& msg);

/// Parse an Rgetattr response.
bool ParseRgetattr(NinePMessage& msg, NinePStat& stat);

/// Parse an Rreaddir response into directory entries.
bool ParseRreaddir(NinePMessage& msg, std::vector<DirEntry>& entries);

/// Parse an Rwrite response.
bool ParseRwrite(NinePMessage& msg, std::uint32_t& count);

/// Parse an Rlcreate response.
bool ParseRlcreate(NinePMessage& msg, Qid& qid, std::uint32_t& iounit);

/// Parse an Rmkdir response.
bool ParseRmkdir(NinePMessage& msg, Qid& qid);

/// Parse an Runlinkat response.
bool ParseRunlinkat(NinePMessage& msg);

/// Parse an Rrenameat response.
bool ParseRrenameat(NinePMessage& msg);

/// Check if the response is an Rlerror and extract the errno.
bool IsRlerror(const NinePMessage& msg, std::uint32_t& error_code);

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_MESSAGE_H
