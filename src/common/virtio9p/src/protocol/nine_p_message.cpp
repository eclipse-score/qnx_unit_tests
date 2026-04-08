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
#include "protocol/nine_p_message.h"

#include <algorithm>
#include <cstring>

namespace virtio9p
{

NinePMessage::NinePMessage(std::vector<std::uint8_t> data) : data_(std::move(data)), read_offset_(kHeaderSize) {}

// --- Encoder helpers ---

void NinePMessage::BeginMessage(MessageType type, std::uint16_t tag)
{
    data_.clear();
    read_offset_ = kHeaderSize;

    // Reserve space for the 4-byte size field
    PutU32(0U);
    PutU8(static_cast<std::uint8_t>(type));
    PutU16(tag);
}

void NinePMessage::EndMessage()
{
    const auto size = static_cast<std::uint32_t>(data_.size());
    // Patch the size field at offset 0
    data_[0] = static_cast<std::uint8_t>(size & 0xFFU);
    data_[1] = static_cast<std::uint8_t>((size >> 8U) & 0xFFU);
    data_[2] = static_cast<std::uint8_t>((size >> 16U) & 0xFFU);
    data_[3] = static_cast<std::uint8_t>((size >> 24U) & 0xFFU);
}

void NinePMessage::PutU8(std::uint8_t value)
{
    data_.push_back(value);
}

void NinePMessage::PutU16(std::uint16_t value)
{
    data_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    data_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void NinePMessage::PutU32(std::uint32_t value)
{
    data_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    data_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    data_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    data_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void NinePMessage::PutU64(std::uint64_t value)
{
    PutU32(static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
    PutU32(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFU));
}

void NinePMessage::PutString(const std::string& value)
{
    PutU16(static_cast<std::uint16_t>(value.size()));
    data_.insert(data_.end(), value.begin(), value.end());
}

void NinePMessage::PutBytes(const std::uint8_t* bytes, std::uint32_t length)
{
    data_.insert(data_.end(), bytes, bytes + length);
}

// --- Decoder helpers ---

std::uint8_t NinePMessage::GetU8()
{
    if (read_offset_ >= data_.size())
    {
        return 0U;
    }
    return data_[read_offset_++];
}

std::uint16_t NinePMessage::GetU16()
{
    if (read_offset_ + 2U > data_.size())
    {
        return 0U;
    }
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data_[read_offset_]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[read_offset_ + 1U]) << 8U));
    read_offset_ += 2U;
    return value;
}

std::uint32_t NinePMessage::GetU32()
{
    if (read_offset_ + 4U > data_.size())
    {
        return 0U;
    }
    const auto value = static_cast<std::uint32_t>(data_[read_offset_]) |
                       (static_cast<std::uint32_t>(data_[read_offset_ + 1U]) << 8U) |
                       (static_cast<std::uint32_t>(data_[read_offset_ + 2U]) << 16U) |
                       (static_cast<std::uint32_t>(data_[read_offset_ + 3U]) << 24U);
    read_offset_ += 4U;
    return value;
}

std::uint64_t NinePMessage::GetU64()
{
    const auto lo = static_cast<std::uint64_t>(GetU32());
    const auto hi = static_cast<std::uint64_t>(GetU32());
    return lo | (hi << 32U);
}

std::string NinePMessage::GetString()
{
    const auto len = GetU16();
    if (read_offset_ + len > data_.size())
    {
        return {};
    }
    std::string result(data_.begin() + read_offset_, data_.begin() + read_offset_ + len);
    read_offset_ += len;
    return result;
}

std::vector<std::uint8_t> NinePMessage::GetBytes(std::uint32_t length)
{
    if (read_offset_ + length > data_.size())
    {
        return {};
    }
    std::vector<std::uint8_t> result(data_.begin() + read_offset_, data_.begin() + read_offset_ + length);
    read_offset_ += length;
    return result;
}

Qid NinePMessage::GetQid()
{
    Qid qid{};
    qid.type = GetU8();
    qid.version = GetU32();
    qid.path = GetU64();
    return qid;
}

// --- Header accessors ---

MessageType NinePMessage::GetType() const
{
    if (data_.size() < kHeaderSize)
    {
        return MessageType::kTlerror;
    }
    return static_cast<MessageType>(data_[4]);
}

std::uint16_t NinePMessage::GetTag() const
{
    if (data_.size() < kHeaderSize)
    {
        return 0U;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[5]) |
                                      (static_cast<std::uint16_t>(data_[6]) << 8U));
}

std::uint32_t NinePMessage::GetSize() const
{
    if (data_.size() < 4U)
    {
        return 0U;
    }
    return static_cast<std::uint32_t>(data_[0]) | (static_cast<std::uint32_t>(data_[1]) << 8U) |
           (static_cast<std::uint32_t>(data_[2]) << 16U) | (static_cast<std::uint32_t>(data_[3]) << 24U);
}

void NinePMessage::ResetReadOffset()
{
    read_offset_ = kHeaderSize;
}

const std::vector<std::uint8_t>& NinePMessage::GetData() const
{
    return data_;
}

std::vector<std::uint8_t>& NinePMessage::GetMutableData()
{
    return data_;
}

// --- High-level message builders ---

NinePMessage BuildTversion(std::uint32_t max_size, const std::string& version)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTversion, kNoTag);
    msg.PutU32(max_size);
    msg.PutString(version);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTattach(std::uint16_t tag,
                          std::uint32_t fid,
                          std::uint32_t afid,
                          const std::string& uname,
                          const std::string& aname,
                          std::uint32_t n_uname)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTattach, tag);
    msg.PutU32(fid);
    msg.PutU32(afid);
    msg.PutString(uname);
    msg.PutString(aname);
    msg.PutU32(n_uname);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTwalk(std::uint16_t tag,
                        std::uint32_t fid,
                        std::uint32_t newfid,
                        const std::vector<std::string>& names)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTwalk, tag);
    msg.PutU32(fid);
    msg.PutU32(newfid);
    msg.PutU16(static_cast<std::uint16_t>(names.size()));
    for (const auto& name : names)
    {
        msg.PutString(name);
    }
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTlopen(std::uint16_t tag, std::uint32_t fid, std::uint32_t flags)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTlopen, tag);
    msg.PutU32(fid);
    msg.PutU32(flags);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTread(std::uint16_t tag, std::uint32_t fid, std::uint64_t offset, std::uint32_t count)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTread, tag);
    msg.PutU32(fid);
    msg.PutU64(offset);
    msg.PutU32(count);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTclunk(std::uint16_t tag, std::uint32_t fid)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTclunk, tag);
    msg.PutU32(fid);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTgetattr(std::uint16_t tag, std::uint32_t fid, std::uint64_t request_mask)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTgetattr, tag);
    msg.PutU32(fid);
    msg.PutU64(request_mask);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTreaddir(std::uint16_t tag, std::uint32_t fid, std::uint64_t offset, std::uint32_t count)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTreaddir, tag);
    msg.PutU32(fid);
    msg.PutU64(offset);
    msg.PutU32(count);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTwrite(std::uint16_t tag,
                         std::uint32_t fid,
                         std::uint64_t offset,
                         const std::uint8_t* data,
                         std::uint32_t count)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTwrite, tag);
    msg.PutU32(fid);
    msg.PutU64(offset);
    msg.PutU32(count);
    msg.PutBytes(data, count);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTlcreate(std::uint16_t tag,
                           std::uint32_t fid,
                           const std::string& name,
                           std::uint32_t flags,
                           std::uint32_t mode,
                           std::uint32_t gid)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTlcreate, tag);
    msg.PutU32(fid);
    msg.PutString(name);
    msg.PutU32(flags);
    msg.PutU32(mode);
    msg.PutU32(gid);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTmkdir(std::uint16_t tag,
                         std::uint32_t dfid,
                         const std::string& name,
                         std::uint32_t mode,
                         std::uint32_t gid)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTmkdir, tag);
    msg.PutU32(dfid);
    msg.PutString(name);
    msg.PutU32(mode);
    msg.PutU32(gid);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTunlinkat(std::uint16_t tag, std::uint32_t dirfid, const std::string& name, std::uint32_t flags)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTunlinkat, tag);
    msg.PutU32(dirfid);
    msg.PutString(name);
    msg.PutU32(flags);
    msg.EndMessage();
    return msg;
}

NinePMessage BuildTrenameat(std::uint16_t tag,
                            std::uint32_t olddirfid,
                            const std::string& oldname,
                            std::uint32_t newdirfid,
                            const std::string& newname)
{
    NinePMessage msg;
    msg.BeginMessage(MessageType::kTrenameat, tag);
    msg.PutU32(olddirfid);
    msg.PutString(oldname);
    msg.PutU32(newdirfid);
    msg.PutString(newname);
    msg.EndMessage();
    return msg;
}

// --- Response parsers ---

bool IsRlerror(const NinePMessage& msg, std::uint32_t& error_code)
{
    if (msg.GetType() != MessageType::kRlerror)
    {
        return false;
    }
    NinePMessage copy(msg.GetData());
    error_code = copy.GetU32();
    return true;
}

bool ParseRversion(NinePMessage& msg, std::uint32_t& max_size, std::string& version)
{
    if (msg.GetType() != MessageType::kRversion)
    {
        return false;
    }
    msg.ResetReadOffset();
    max_size = msg.GetU32();
    version = msg.GetString();
    return true;
}

bool ParseRattach(NinePMessage& msg, Qid& qid)
{
    if (msg.GetType() != MessageType::kRattach)
    {
        return false;
    }
    msg.ResetReadOffset();
    qid = msg.GetQid();
    return true;
}

bool ParseRwalk(NinePMessage& msg, std::vector<Qid>& qids)
{
    if (msg.GetType() != MessageType::kRwalk)
    {
        return false;
    }
    msg.ResetReadOffset();
    const auto nwqid = msg.GetU16();
    qids.reserve(nwqid);
    for (std::uint16_t i = 0U; i < nwqid; ++i)
    {
        qids.push_back(msg.GetQid());
    }
    return true;
}

bool ParseRlopen(NinePMessage& msg, Qid& qid, std::uint32_t& iounit)
{
    if (msg.GetType() != MessageType::kRlopen)
    {
        return false;
    }
    msg.ResetReadOffset();
    qid = msg.GetQid();
    iounit = msg.GetU32();
    return true;
}

bool ParseRread(NinePMessage& msg, std::vector<std::uint8_t>& data)
{
    if (msg.GetType() != MessageType::kRread)
    {
        return false;
    }
    msg.ResetReadOffset();
    const auto count = msg.GetU32();
    data = msg.GetBytes(count);
    return true;
}

bool ParseRclunk(NinePMessage& msg)
{
    return msg.GetType() == MessageType::kRclunk;
}

bool ParseRgetattr(NinePMessage& msg, NinePStat& stat)
{
    if (msg.GetType() != MessageType::kRgetattr)
    {
        return false;
    }
    msg.ResetReadOffset();
    stat.valid = msg.GetU64();
    stat.qid = msg.GetQid();
    stat.mode = msg.GetU32();
    stat.uid = msg.GetU32();
    stat.gid = msg.GetU32();
    stat.nlink = msg.GetU64();
    stat.rdev = msg.GetU64();
    stat.size = msg.GetU64();
    stat.blksize = msg.GetU64();
    stat.blocks = msg.GetU64();
    stat.atime_sec = msg.GetU64();
    stat.atime_nsec = msg.GetU64();
    stat.mtime_sec = msg.GetU64();
    stat.mtime_nsec = msg.GetU64();
    stat.ctime_sec = msg.GetU64();
    stat.ctime_nsec = msg.GetU64();
    stat.btime_sec = msg.GetU64();
    stat.btime_nsec = msg.GetU64();
    stat.gen = msg.GetU64();
    stat.data_version = msg.GetU64();
    return true;
}

bool ParseRreaddir(NinePMessage& msg, std::vector<DirEntry>& entries)
{
    if (msg.GetType() != MessageType::kRreaddir)
    {
        return false;
    }
    msg.ResetReadOffset();
    const auto count = msg.GetU32();

    // Parse directory entries from the data blob
    // Each entry: [qid(13)][offset(8)][type(1)][name_len(2)][name]
    std::uint32_t bytes_consumed = 0U;
    while (bytes_consumed < count)
    {
        DirEntry entry{};
        entry.qid = msg.GetQid();
        entry.offset = msg.GetU64();
        entry.type = msg.GetU8();
        entry.name = msg.GetString();
        bytes_consumed += 13U + 8U + 1U + 2U + static_cast<std::uint32_t>(entry.name.size());
        entries.push_back(std::move(entry));
    }

    return true;
}

bool ParseRwrite(NinePMessage& msg, std::uint32_t& count)
{
    if (msg.GetType() != MessageType::kRwrite)
    {
        return false;
    }
    msg.ResetReadOffset();
    count = msg.GetU32();
    return true;
}

bool ParseRlcreate(NinePMessage& msg, Qid& qid, std::uint32_t& iounit)
{
    if (msg.GetType() != MessageType::kRlcreate)
    {
        return false;
    }
    msg.ResetReadOffset();
    qid = msg.GetQid();
    iounit = msg.GetU32();
    return true;
}

bool ParseRmkdir(NinePMessage& msg, Qid& qid)
{
    if (msg.GetType() != MessageType::kRmkdir)
    {
        return false;
    }
    msg.ResetReadOffset();
    qid = msg.GetQid();
    return true;
}

bool ParseRunlinkat(NinePMessage& msg)
{
    return msg.GetType() == MessageType::kRunlinkat;
}

bool ParseRrenameat(NinePMessage& msg)
{
    return msg.GetType() == MessageType::kRrenameat;
}

}  // namespace virtio9p
