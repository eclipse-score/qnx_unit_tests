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
#include "resmgr/fs_virtio9p.h"
#include "log/log.h"
#include "protocol/nine_p_message.h"
#include "protocol/nine_p_session.h"
#include "protocol/nine_p_types.h"
#include "transport/mmio_transport.h"
#include "transport/pci_transport.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include <sys/resmgr.h>
#include <sys/stat.h>
#include <unistd.h>

namespace virtio9p
{

namespace
{

/// Global session pointer accessible from C handler callbacks.
NinePSession* g_session = nullptr;

/// Extended OCB embedding per-open virtio9p state directly.
struct Virtio9pExtOcb
{
    iofunc_ocb_t iofunc_ocb;  // Must be first member
    iofunc_attr_t file_attr;  // Per-file attr (mode, size, etc.)
    Virtio9pOcb data;
};

/// Last allocated extended OCB (valid during single-threaded dispatch).
Virtio9pExtOcb* g_last_ext_ocb = nullptr;

/// QNX O_APPEND value (differs from Linux).
constexpr std::uint32_t kQnxOAppend = 0x08U;

/// Translate QNX ioflag to Linux open flags for 9P2000.L.
/// QNX connect.ioflag uses _IO_FLAG_RD(0x01)/_IO_FLAG_WR(0x02) as a bitmask
/// for access mode, while Linux uses O_RDONLY(0)/O_WRONLY(1)/O_RDWR(2) as an
/// enumeration.  Flags above the _IO_FLAG_MASK (O_TRUNC, O_APPEND, etc.) match
/// QNX fcntl.h values and need individual translation where they differ.
/// O_CREAT is handled separately via Tlcreate and stripped here.
std::uint32_t TranslateQnxToLinuxFlags(std::uint32_t qnx_ioflag)
{
    // Access mode: QNX _IO_FLAG_RD=0x01, _IO_FLAG_WR=0x02 (bitmask)
    //              Linux O_RDONLY=0, O_WRONLY=1, O_RDWR=2 (enum)
    const bool has_read = (qnx_ioflag & 0x01U) != 0U;
    const bool has_write = (qnx_ioflag & 0x02U) != 0U;

    std::uint32_t linux_flags = 0U;  // O_RDONLY
    if (has_read && has_write)
    {
        linux_flags = kLinuxORdwr;
    }
    else if (has_write)
    {
        linux_flags = kLinuxOWronly;
    }

    if ((qnx_ioflag & O_TRUNC) != 0U)
    {
        linux_flags |= kLinuxOTrunc;
    }
    if ((qnx_ioflag & kQnxOAppend) != 0U)
    {
        linux_flags |= kLinuxOAppend;
    }

    return linux_flags;
}

iofunc_ocb_t* VirtioOcbCalloc(resmgr_context_t* /*ctp*/, iofunc_attr_t* /*attr*/)
{
    auto* ext = static_cast<Virtio9pExtOcb*>(calloc(1, sizeof(Virtio9pExtOcb)));
    g_last_ext_ocb = ext;
    return ext != nullptr ? &ext->iofunc_ocb : nullptr;
}

void VirtioOcbFree(iofunc_ocb_t* ocb)
{
    free(ocb);
}

iofunc_funcs_t g_ocb_funcs{};
iofunc_mount_t g_mount{};

// --- Connect handlers ---

int ConnectOpen(resmgr_context_t* ctp, io_open_t* msg, RESMGR_HANDLE_T* /*handle*/, void* extra)
{
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    // Get the path being opened (relative to mount point)
    const char* path = msg->connect.path;
    if (path == nullptr || path[0] == '\0')
    {
        path = "/";
    }

    const auto qnx_ioflag = static_cast<std::uint32_t>(msg->connect.ioflag);
    const bool want_create = (qnx_ioflag & O_CREAT) != 0U;
    const auto linux_flags = TranslateQnxToLinuxFlags(qnx_ioflag);

    std::uint32_t fid = 0U;
    std::uint32_t iounit = 0U;
    bool is_dir = false;
    NinePStat stat{};

    // Try to walk to the requested path
    auto rc = g_session->Walk(path, fid);
    if (rc == 0)
    {
        // Path exists — open it with translated flags
        rc = g_session->Open(fid, linux_flags, iounit);
        if (rc != 0)
        {
            g_session->Clunk(fid);
            return -rc;
        }
        g_session->GetAttr(fid, stat);
        is_dir = (stat.mode & 0040000U) != 0U;
    }
    else if (rc == -ENOENT && want_create)
    {
        // Path does not exist and O_CREAT was requested — create via Tlcreate
        std::uint32_t parent_fid = 0U;
        std::string basename;
        rc = g_session->WalkParent(path, parent_fid, basename);
        if (rc != 0)
        {
            return -rc;
        }

        Qid new_qid{};
        auto create_flags = linux_flags | kLinuxOCreat;
        rc = g_session->Create(parent_fid, basename, create_flags, 0644U, 0U, new_qid, iounit);
        if (rc != 0)
        {
            g_session->Clunk(parent_fid);
            return -rc;
        }
        // After Tlcreate, parent_fid now references the new open file
        fid = parent_fid;
        is_dir = false;
        stat.mode = 0100644U;
        stat.size = 0U;
    }
    else
    {
        return -rc;
    }

    // Let iofunc handle the local OCB setup using the actual opened object's
    // mode.  The mounted namespace handle is always a directory, which causes
    // write opens on regular files to be rejected as EISDIR.
    iofunc_attr_t open_attr;
    iofunc_attr_init(&open_attr, static_cast<mode_t>(stat.mode), nullptr, nullptr);
    open_attr.mount = &g_mount;

    // Backend create/truncate has already been handled via 9P, so strip
    // creation-specific flags before passing the request into the QNX helper
    // to avoid re-driving local create semantics.
    const auto original_ioflag = msg->connect.ioflag;
    msg->connect.ioflag &= static_cast<std::uint32_t>(~(O_CREAT | O_EXCL | O_TRUNC));
    g_last_ext_ocb = nullptr;
    auto result = iofunc_open_default(ctp, msg, &open_attr, extra);
    msg->connect.ioflag = original_ioflag;
    if (result != EOK)
    {
        g_session->Clunk(fid);
        return result;
    }

    // Store per-open state directly in the extended OCB
    if (g_last_ext_ocb != nullptr)
    {
        g_last_ext_ocb->data.fid = fid;
        g_last_ext_ocb->data.offset = 0U;
        g_last_ext_ocb->data.iounit = iounit;
        g_last_ext_ocb->data.is_directory = is_dir;
        g_last_ext_ocb->data.open_flags = linux_flags;

        // Initialize per-file attr so iofunc_read/write_verify sees correct file type
        iofunc_attr_init(&g_last_ext_ocb->file_attr, static_cast<mode_t>(stat.mode), nullptr, nullptr);
        g_last_ext_ocb->file_attr.nbytes = static_cast<off_t>(stat.size);
        g_last_ext_ocb->file_attr.mount = &g_mount;
        g_last_ext_ocb->iofunc_ocb.attr = &g_last_ext_ocb->file_attr;
    }

    return EOK;
}

// --- I/O handlers ---

int IoRead(resmgr_context_t* ctp, io_read_t* msg, void* vocb)
{
    auto* ocb = static_cast<iofunc_ocb_t*>(vocb);
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    // Verify the read is allowed
    auto rc = iofunc_read_verify(ctp, msg, ocb, nullptr);
    if (rc != EOK)
    {
        V9P_ERR("IoRead iofunc_read_verify failed rc=%d", rc);
        return rc;
    }

    // Access per-open state from extended OCB
    auto* ext = reinterpret_cast<Virtio9pExtOcb*>(vocb);
    auto& ocb_data = ext->data;
    const auto nbytes = msg->i.nbytes;

    if (ocb_data.is_directory)
    {
        // Directory read: use readdir
        std::vector<DirEntry> entries;
        auto result = g_session->ReadDir(ocb_data.fid, ocb_data.offset, nbytes, entries);
        if (result != 0)
        {
            return -result;
        }

        // Format entries as QNX dirent structures
        std::vector<std::uint8_t> buf;
        for (const auto& entry : entries)
        {
            // Build struct dirent in buffer — d_name is a flexible array member
            const auto name_len = entry.name.size();
            // offsetof(dirent, d_name) + name_len + 1 (NUL), rounded up to 8
            const auto reclen = (offsetof(struct dirent, d_name) + name_len + 1U + 7U) & ~static_cast<std::size_t>(7U);

            std::vector<std::uint8_t> entry_buf(reclen, 0U);
            auto* de = reinterpret_cast<struct dirent*>(entry_buf.data());
            de->d_ino = static_cast<ino_t>(entry.qid.path);
            de->d_offset = static_cast<off_t>(entry.offset);
            de->d_reclen = static_cast<std::int16_t>(reclen);
            de->d_namelen = static_cast<std::int16_t>(name_len);
            std::memcpy(de->d_name, entry.name.c_str(), name_len + 1U);

            buf.insert(buf.end(), entry_buf.begin(), entry_buf.end());
            ocb_data.offset = entry.offset;
        }

        if (buf.empty())
        {
            // End of directory
            _IO_SET_READ_NBYTES(ctp, 0);
            return EOK;
        }

        const auto reply_len = static_cast<int>(buf.size() < nbytes ? buf.size() : nbytes);
        MsgReply(ctp->rcvid, reply_len, buf.data(), static_cast<std::size_t>(reply_len));
        ocb->attr->nbytes = static_cast<off_t>(reply_len);
        return _RESMGR_NOREPLY;
    }
    else
    {
        // Regular file read
        std::vector<std::uint8_t> data;
        auto result = g_session->Read(ocb_data.fid, ocb_data.offset, nbytes, data);
        if (result < 0)
        {
            return -result;
        }

        if (data.empty())
        {
            // EOF
            _IO_SET_READ_NBYTES(ctp, 0);
            return EOK;
        }

        MsgReply(ctp->rcvid, static_cast<int>(data.size()), data.data(), data.size());
        ocb_data.offset += data.size();
        ocb->attr->nbytes = static_cast<off_t>(data.size());
        return _RESMGR_NOREPLY;
    }
}

int IoStat(resmgr_context_t* ctp, io_stat_t* msg, void* vocb)
{
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    auto* ext = reinterpret_cast<Virtio9pExtOcb*>(vocb);
    const auto& ocb_data = ext->data;

    NinePStat p9stat{};
    auto rc = g_session->GetAttr(ocb_data.fid, p9stat);
    if (rc != 0)
    {
        return -rc;
    }

    // Fill the reply stat structure directly in the message union
    std::memset(&msg->o, 0, sizeof(msg->o));
    msg->o.st_mode = static_cast<mode_t>(p9stat.mode);
    msg->o.st_uid = static_cast<uid_t>(p9stat.uid);
    msg->o.st_gid = static_cast<gid_t>(p9stat.gid);
    msg->o.st_nlink = static_cast<nlink_t>(p9stat.nlink);
    msg->o.st_size = static_cast<off_t>(p9stat.size);
    msg->o.st_ino = static_cast<ino_t>(p9stat.qid.path);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
    return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

int IoWrite(resmgr_context_t* ctp, io_write_t* msg, void* vocb)
{
    auto* ocb = static_cast<iofunc_ocb_t*>(vocb);
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    auto rc = iofunc_write_verify(ctp, msg, ocb, nullptr);
    if (rc != EOK)
    {
        V9P_ERR("IoWrite iofunc_write_verify failed rc=%d", rc);
        return rc;
    }

    auto* ext = reinterpret_cast<Virtio9pExtOcb*>(vocb);
    auto& ocb_data = ext->data;
    const auto nbytes = static_cast<std::uint32_t>(_IO_WRITE_GET_NBYTES(msg));

    if (nbytes == 0U)
    {
        _IO_SET_WRITE_NBYTES(ctp, 0);
        return EOK;
    }

    // Determine offset: pwrite() uses explicit offset, otherwise use sequential offset
    std::uint64_t offset = ocb_data.offset;
    const auto xtype = msg->i.xtype & _IO_XTYPE_MASK;
    std::size_t header_size = sizeof(io_write_t);
    if (xtype == _IO_XTYPE_OFFSET)
    {
        // pwrite: offset is in the xtype_offset struct following the header
        const auto* xoff = reinterpret_cast<const struct _xtype_offset*>(&msg[1]);
        offset = static_cast<std::uint64_t>(xoff->offset);
        header_size += sizeof(struct _xtype_offset);
    }
    else if (xtype != _IO_XTYPE_NONE)
    {
        return ENOSYS;
    }

    const auto header_end = static_cast<std::int64_t>(ctp->offset) + static_cast<std::int64_t>(header_size);
    const auto inline_bytes_available_i64 = static_cast<std::int64_t>(ctp->info.msglen) > header_end
                                                ? (static_cast<std::int64_t>(ctp->info.msglen) - header_end)
                                                : 0;
    const auto inline_bytes_available = static_cast<std::size_t>(inline_bytes_available_i64);
    const auto inline_bytes = static_cast<std::uint32_t>(
        (inline_bytes_available < static_cast<std::size_t>(nbytes)) ? inline_bytes_available : nbytes);

    const auto* inline_data =
        reinterpret_cast<const std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(msg) + header_size);

    const auto chunk_size = (ocb_data.iounit > 0U) ? ocb_data.iounit : (kMaxMessageSize - 128U);
    std::uint32_t total_written = 0U;

    while (total_written < nbytes)
    {
        auto to_write = nbytes - total_written;
        if (to_write > chunk_size)
        {
            to_write = chunk_size;
        }

        std::vector<std::uint8_t> buf(to_write);

        std::uint32_t copied = 0U;
        if (total_written < inline_bytes)
        {
            copied = inline_bytes - total_written;
            if (copied > to_write)
            {
                copied = to_write;
            }
            std::memcpy(buf.data(), inline_data + total_written, copied);
        }

        if (copied < to_write)
        {
            const auto overflow_offset_i64 =
                static_cast<std::int64_t>(ctp->offset) + static_cast<std::int64_t>(header_size) +
                static_cast<std::int64_t>(total_written) + static_cast<std::int64_t>(copied);
            const auto overflow_offset = static_cast<int>(overflow_offset_i64);
            auto bytes_read =
                resmgr_msgread(ctp, buf.data() + copied, static_cast<int>(to_write - copied), overflow_offset);
            if (bytes_read < 0)
            {
                return errno;
            }
            copied += static_cast<std::uint32_t>(bytes_read);
        }

        auto written = g_session->Write(ocb_data.fid, offset + total_written, buf.data(), copied);
        if (written < 0)
        {
            return -written;
        }
        total_written += static_cast<std::uint32_t>(written);

        if (static_cast<std::uint32_t>(written) < copied)
        {
            break;  // Short write from server
        }
    }

    // Update sequential offset unless pwrite
    if (xtype != _IO_XTYPE_OFFSET)
    {
        ocb_data.offset += total_written;
    }

    if (static_cast<off_t>(offset + total_written) > ocb->attr->nbytes)
    {
        ocb->attr->nbytes = static_cast<off_t>(offset + total_written);
    }

    _IO_SET_WRITE_NBYTES(ctp, static_cast<int>(total_written));
    return EOK;
}

int ConnectMknod(resmgr_context_t* /*ctp*/, io_mknod_t* msg, RESMGR_HANDLE_T* /*handle*/, void* /*extra*/)
{
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    const char* path = msg->connect.path;
    if (path == nullptr || path[0] == '\0')
    {
        return EINVAL;
    }

    const auto mode = static_cast<std::uint32_t>(msg->connect.mode);
    if (!S_ISDIR(mode))
    {
        // Only mkdir is supported; mknod for devices/fifos is not needed
        return ENOTSUP;
    }

    std::uint32_t parent_fid = 0U;
    std::string basename;
    auto rc = g_session->WalkParent(path, parent_fid, basename);
    if (rc != 0)
    {
        return -rc;
    }

    rc = g_session->Mkdir(parent_fid, basename, mode & 0777U, 0U);
    g_session->Clunk(parent_fid);
    if (rc != 0)
    {
        return -rc;
    }

    return EOK;
}

int ConnectUnlink(resmgr_context_t* /*ctp*/, io_unlink_t* msg, RESMGR_HANDLE_T* /*handle*/, void* /*extra*/)
{
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    const char* path = msg->connect.path;
    if (path == nullptr || path[0] == '\0')
    {
        return EINVAL;
    }

    std::uint32_t parent_fid = 0U;
    std::string basename;
    auto rc = g_session->WalkParent(path, parent_fid, basename);
    if (rc != 0)
    {
        return -rc;
    }

    // Check if the target is a directory to set AT_REMOVEDIR
    std::uint32_t target_fid = 0U;
    std::uint32_t unlink_flags = 0U;
    std::string full_path(path);
    rc = g_session->Walk(full_path, target_fid);
    if (rc == 0)
    {
        NinePStat stat{};
        if (g_session->GetAttr(target_fid, stat) == 0)
        {
            if ((stat.mode & 0040000U) != 0U)
            {
                unlink_flags = kLinuxAtRemovedir;
            }
        }
        g_session->Clunk(target_fid);
    }

    rc = g_session->Unlink(parent_fid, basename, unlink_flags);
    g_session->Clunk(parent_fid);
    if (rc != 0)
    {
        return -rc;
    }

    return EOK;
}

int ConnectRename(resmgr_context_t* /*ctp*/,
                  io_rename_t* msg,
                  RESMGR_HANDLE_T* /*handle*/,
                  io_rename_extra_t* /*extra*/)
{
    if (g_session == nullptr)
    {
        return ENOSYS;
    }

    const char* old_path = msg->connect.path;
    if (old_path == nullptr || old_path[0] == '\0')
    {
        return EINVAL;
    }

    // The new name follows the old path in the message data, after the connect header
    const char* new_path = old_path + std::strlen(old_path) + 1U;

    std::uint32_t old_parent_fid = 0U;
    std::string old_basename;
    auto rc = g_session->WalkParent(old_path, old_parent_fid, old_basename);
    if (rc != 0)
    {
        return -rc;
    }

    std::uint32_t new_parent_fid = 0U;
    std::string new_basename;
    rc = g_session->WalkParent(new_path, new_parent_fid, new_basename);
    if (rc != 0)
    {
        g_session->Clunk(old_parent_fid);
        return -rc;
    }

    rc = g_session->Rename(old_parent_fid, old_basename, new_parent_fid, new_basename);
    g_session->Clunk(old_parent_fid);
    g_session->Clunk(new_parent_fid);
    if (rc != 0)
    {
        return -rc;
    }

    return EOK;
}

int IoCloseOcb(resmgr_context_t* ctp, void* reserved, void* vocb)
{
    auto* ocb = static_cast<iofunc_ocb_t*>(vocb);
    if (g_session != nullptr)
    {
        auto* ext = reinterpret_cast<Virtio9pExtOcb*>(vocb);
        g_session->Clunk(ext->data.fid);
    }
    return iofunc_close_ocb_default(ctp, reserved, ocb);
}

}  // namespace

std::unique_ptr<Transport> CreateTransport(const FsConfig& config)
{
    if (config.transport_type == "mmio")
    {
        MmioConfig mmio_cfg{};
        mmio_cfg.base_address = config.mmio_base;
        mmio_cfg.irq = config.irq;
        return std::make_unique<MmioTransportImpl>(mmio_cfg);
    }
    return std::make_unique<PciTransportImpl>();
}

std::int32_t FsVirtio9pMain(int argc, char* argv[])
{
    FsConfig config{};
    if (ParseArgs(argc, argv, config) != 0)
    {
        fprintf(stderr, "Usage: fs-virtio9p [-d] [-o smem=<addr>,irq=<n>,transport=mmio] <mountpoint>\n");
        return 1;
    }

    log::Initialize();

    V9P_INFO("mounting at %s via %s transport", config.mount_point.c_str(), config.transport_type.c_str());

    // Daemonize before any resource allocation so the child process owns
    // all QNX-specific resources (IRQ attachments, channels, DMA buffers).
    // These are per-process and cannot survive fork().
    int notify_fds[2] = {-1, -1};
    if (config.daemonize)
    {
        if (pipe(notify_fds) != 0)
        {
            V9P_ERR("pipe failed: %s", strerror(errno));
            return 1;
        }
        const auto pid = fork();
        if (pid < 0)
        {
            V9P_ERR("fork failed: %s", strerror(errno));
            return 1;
        }
        if (pid > 0)
        {
            // Parent: wait for child to signal mount readiness
            close(notify_fds[1]);
            char status = 1;
            read(notify_fds[0], &status, 1);
            close(notify_fds[0]);
            _exit(status);
        }
        // Child: detach from terminal, continue with all initialization
        close(notify_fds[0]);
        setsid();
    }

    // Gain I/O privilege level — required for mmap_device_memory,
    // InterruptAttachEvent, and PCI device ownership on QNX.
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1)
    {
        V9P_ERR("ThreadCtl failed: %s", strerror(errno));
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    // Initialize transport (must happen in the final process that will serve)
    auto transport = CreateTransport(config);
    auto rc = transport->Initialize();
    if (rc != 0)
    {
        V9P_ERR("transport init failed: %s", strerror(-rc));
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    // Read mount tag from device config
    std::string mount_tag;
    rc = transport->GetMountTag(mount_tag);
    if (rc != 0)
    {
        V9P_ERR("failed to read mount tag, using empty aname");
        mount_tag = "";
    }
    else
    {
        V9P_INFO("mount tag: '%s'", mount_tag.c_str());
    }

    // Initialize 9P session
    NinePSession session(*transport);
    rc = session.Initialize(mount_tag);
    if (rc != 0)
    {
        V9P_ERR("9P session init failed: %s", strerror(-rc));
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    g_session = &session;

    // Set up QNX resource manager
    dispatch_t* dpp = dispatch_create();
    if (dpp == nullptr)
    {
        V9P_ERR("dispatch_create failed");
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    resmgr_attr_t rattr{};
    rattr.nparts_max = 1;
    rattr.msg_max_size = static_cast<int>(kMaxMessageSize);

    resmgr_connect_funcs_t connect_funcs;
    resmgr_io_funcs_t io_funcs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);

    // Override handlers
    connect_funcs.open = ConnectOpen;
    connect_funcs.mknod = ConnectMknod;
    connect_funcs.unlink = ConnectUnlink;
    connect_funcs.rename = ConnectRename;
    io_funcs.read = IoRead;
    io_funcs.write = IoWrite;
    io_funcs.stat = IoStat;
    io_funcs.close_ocb = IoCloseOcb;

    g_ocb_funcs.nfuncs = _IOFUNC_NFUNCS;
    g_ocb_funcs.ocb_calloc = VirtioOcbCalloc;
    g_ocb_funcs.ocb_free = VirtioOcbFree;
    g_mount.funcs = &g_ocb_funcs;

    iofunc_attr_t attr;
    iofunc_attr_init(&attr, S_IFDIR | 0777, nullptr, nullptr);
    attr.mount = &g_mount;

    auto id = resmgr_attach(
        dpp, &rattr, config.mount_point.c_str(), _FTYPE_ANY, _RESMGR_FLAG_DIR, &connect_funcs, &io_funcs, &attr);
    if (id == -1)
    {
        V9P_ERR("resmgr_attach failed: %s", strerror(errno));
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    dispatch_context_t* ctp = dispatch_context_alloc(dpp);
    if (ctp == nullptr)
    {
        V9P_ERR("dispatch_context_alloc failed");
        if (config.daemonize)
        {
            char status = 1;
            write(notify_fds[1], &status, 1);
            close(notify_fds[1]);
        }
        return 1;
    }

    V9P_INFO("serving at %s", config.mount_point.c_str());

    // Signal parent that mount is ready, then close notification pipe
    if (config.daemonize)
    {
        char status = 0;
        write(notify_fds[1], &status, 1);
        close(notify_fds[1]);
    }

    // Main dispatch loop
    while (true)
    {
        ctp = dispatch_block(ctp);
        if (ctp == nullptr)
        {
            V9P_ERR("dispatch_block failed: %s", strerror(errno));
            break;
        }
        dispatch_handler(ctp);
    }

    g_session = nullptr;

    transport->Shutdown();
    return 0;
}

}  // namespace virtio9p
