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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_SESSION_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_SESSION_H

#include "protocol/fid_pool.h"
#include "protocol/nine_p_message.h"
#include "protocol/nine_p_types.h"
#include "transport/transport.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace virtio9p
{

/// Manages a 9P2000.L session: version negotiation, root attach, path walking,
/// and all file operations.
///
/// This is the high-level API used by the resource manager handlers to perform
/// 9P operations. It owns the fid pool and uses the transport layer to exchange
/// messages with the server.
class NinePSession final
{
  public:
    /// Construct a session bound to the given transport.
    explicit NinePSession(Transport& transport);

    ~NinePSession() = default;

    NinePSession(const NinePSession&) = delete;
    NinePSession& operator=(const NinePSession&) = delete;

    /// Perform version negotiation and root attach.
    /// @param aname The attach name (mount tag from virtio config).
    /// @return 0 on success, negative errno on failure.
    std::int32_t Initialize(const std::string& aname);

    /// Walk from root to the given path and return the fid for the result.
    /// @param path Absolute path relative to the mount root (e.g. "dir/file.txt").
    /// @param out_fid The allocated fid for the walked path.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Walk(const std::string& path, std::uint32_t& out_fid);

    /// Open a file (Tlopen) with the given POSIX flags.
    /// @param fid The fid obtained from Walk().
    /// @param flags POSIX open flags (O_RDONLY etc.).
    /// @param out_iounit The server's preferred I/O unit size.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Open(std::uint32_t fid, std::uint32_t flags, std::uint32_t& out_iounit);

    /// Read data from an open file.
    /// @param fid The fid of the open file.
    /// @param offset File offset to read from.
    /// @param count Maximum bytes to read.
    /// @param out_data Read data is appended here.
    /// @return Number of bytes read on success, negative errno on failure.
    std::int32_t Read(std::uint32_t fid,
                      std::uint64_t offset,
                      std::uint32_t count,
                      std::vector<std::uint8_t>& out_data);

    /// Get file attributes (Tgetattr).
    /// @param fid The fid of the file.
    /// @param out_stat The returned attributes.
    /// @return 0 on success, negative errno on failure.
    std::int32_t GetAttr(std::uint32_t fid, NinePStat& out_stat);

    /// Read directory entries (Treaddir).
    /// @param fid The fid of the open directory.
    /// @param offset Directory cookie (0 for first call).
    /// @param count Maximum bytes of directory data.
    /// @param out_entries Parsed directory entries.
    /// @return 0 on success, negative errno on failure.
    std::int32_t ReadDir(std::uint32_t fid,
                         std::uint64_t offset,
                         std::uint32_t count,
                         std::vector<DirEntry>& out_entries);

    /// Clunk (close) a fid.
    /// @param fid The fid to close.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Clunk(std::uint32_t fid);

    /// Write data to an open file.
    /// @param fid The fid of the open file.
    /// @param offset File offset to write at.
    /// @param data Pointer to the data to write.
    /// @param count Number of bytes to write.
    /// @return Number of bytes written on success, negative errno on failure.
    std::int32_t Write(std::uint32_t fid, std::uint64_t offset, const std::uint8_t* data, std::uint32_t count);

    /// Create and open a new file in a directory.
    /// @param parent_fid Fid of the parent directory; after success it references the new file.
    /// @param name Name of the file to create.
    /// @param flags Linux open flags.
    /// @param mode File permission mode.
    /// @param gid Group ID.
    /// @param out_qid QID of the created file.
    /// @param out_iounit Server-preferred I/O unit size.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Create(std::uint32_t parent_fid,
                        const std::string& name,
                        std::uint32_t flags,
                        std::uint32_t mode,
                        std::uint32_t gid,
                        Qid& out_qid,
                        std::uint32_t& out_iounit);

    /// Create a directory.
    /// @param parent_fid Fid of the parent directory.
    /// @param name Name of the directory to create.
    /// @param mode Permission mode.
    /// @param gid Group ID.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Mkdir(std::uint32_t parent_fid, const std::string& name, std::uint32_t mode, std::uint32_t gid);

    /// Unlink a file or directory.
    /// @param dirfid Fid of the parent directory.
    /// @param name Name of the entry to unlink.
    /// @param flags 0 for files, 0x200 (AT_REMOVEDIR) for directories.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Unlink(std::uint32_t dirfid, const std::string& name, std::uint32_t flags);

    /// Rename a file or directory.
    /// @param olddirfid Fid of the old parent directory.
    /// @param oldname Old name.
    /// @param newdirfid Fid of the new parent directory.
    /// @param newname New name.
    /// @return 0 on success, negative errno on failure.
    std::int32_t Rename(std::uint32_t olddirfid,
                        const std::string& oldname,
                        std::uint32_t newdirfid,
                        const std::string& newname);

    /// Walk to the parent directory of a path and return the basename.
    /// @param path Path relative to mount root.
    /// @param out_parent_fid Fid of the parent directory.
    /// @param out_basename The filename component.
    /// @return 0 on success, negative errno on failure.
    std::int32_t WalkParent(const std::string& path, std::uint32_t& out_parent_fid, std::string& out_basename);

    /// Get the root fid.
    std::uint32_t GetRootFid() const;

    /// Get the negotiated message size.
    std::uint32_t GetMessageSize() const;

  private:
    /// Send a request and receive the response, returning error if Rlerror.
    std::int32_t Transact(NinePMessage& request, NinePMessage& response);

    /// Allocate the next tag for a request.
    std::uint16_t NextTag();

    /// Re-initialize the session (version + attach) after a transport reset.
    std::int32_t Reinitialize();

    static constexpr std::int32_t kMaxRetries = 3;

    Transport& transport_;
    FidPool fid_pool_{};
    std::uint32_t root_fid_{0U};
    std::uint32_t message_size_{kMaxMessageSize};
    std::string aname_{};
    bool reinitializing_{false};

    mutable std::mutex tag_mutex_{};
    std::uint16_t next_tag_{0U};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_NINE_P_SESSION_H
