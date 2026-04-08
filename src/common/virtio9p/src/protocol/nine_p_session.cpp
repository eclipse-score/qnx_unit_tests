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
#include "protocol/nine_p_session.h"
#include "log/log.h"
#include "protocol/nine_p_message.h"

#include <cerrno>
#include <sstream>

namespace virtio9p
{

NinePSession::NinePSession(Transport& transport) : transport_(transport) {}

std::int32_t NinePSession::Transact(NinePMessage& request, NinePMessage& response)
{
    for (std::int32_t attempt = 0; attempt < kMaxRetries; ++attempt)
    {
        std::vector<std::uint8_t> response_data;
        auto rc = transport_.Exchange(request.GetData(), response_data);
        if (rc != 0)
        {
            V9P_WARN("Transact: Exchange failed (rc=%d), attempt %d/%d", rc, attempt + 1, kMaxRetries);
            // During re-initialization (Tversion/Tattach), do not recurse.
            if (reinitializing_)
            {
                return rc;
            }
            // The transport has already reset the device.  Re-establish the
            // 9P session (version + attach) so the next attempt starts clean.
            auto reinit_rc = Reinitialize();
            if (reinit_rc != 0)
            {
                V9P_ERR("Transact: session re-init failed: %d", reinit_rc);
                return rc;
            }
            continue;
        }

        response = NinePMessage(std::move(response_data));

        // Check for error response
        std::uint32_t error_code = 0U;
        if (IsRlerror(response, error_code))
        {
            return -static_cast<std::int32_t>(error_code);
        }

        return 0;
    }

    V9P_ERR("Transact: all %d retry attempts exhausted", kMaxRetries);
    return -EIO;
}

std::int32_t NinePSession::Reinitialize()
{
    V9P_INFO("Re-initializing 9P session (aname='%s')", aname_.c_str());
    fid_pool_.Reset();
    next_tag_ = 0U;
    reinitializing_ = true;
    auto rc = Initialize(aname_);
    reinitializing_ = false;
    return rc;
}

std::uint16_t NinePSession::NextTag()
{
    std::lock_guard<std::mutex> lock(tag_mutex_);
    return next_tag_++;
}

std::int32_t NinePSession::Initialize(const std::string& aname)
{
    aname_ = aname;

    // Step 1: Version negotiation
    auto tversion = BuildTversion(kMaxMessageSize, kProtocolVersion);
    NinePMessage rversion;
    auto rc = Transact(tversion, rversion);
    if (rc != 0)
    {
        V9P_ERR("Tversion failed: %d", rc);
        return rc;
    }

    std::uint32_t negotiated_size = 0U;
    std::string negotiated_version;
    if (!ParseRversion(rversion, negotiated_size, negotiated_version))
    {
        V9P_ERR("bad Rversion");
        return -EIO;
    }

    if (negotiated_version != kProtocolVersion)
    {
        V9P_DBG("server rejected 9P2000.L, got: %s", negotiated_version.c_str());
        return -ENOTSUP;
    }

    message_size_ = (negotiated_size < kMaxMessageSize) ? negotiated_size : kMaxMessageSize;

    // Step 2: Attach to the root
    root_fid_ = fid_pool_.Allocate();
    const auto tag = NextTag();
    auto tattach = BuildTattach(tag, root_fid_, kNoFid, "", aname, 0U);
    NinePMessage rattach;
    rc = Transact(tattach, rattach);
    if (rc != 0)
    {
        V9P_ERR("Tattach failed: %d", rc);
        fid_pool_.Release(root_fid_);
        return rc;
    }

    Qid root_qid{};
    if (!ParseRattach(rattach, root_qid))
    {
        V9P_ERR("bad Rattach");
        fid_pool_.Release(root_fid_);
        return -EIO;
    }

    V9P_INFO("attached to '%s', msize=%u", aname.c_str(), message_size_);
    return 0;
}

std::int32_t NinePSession::Walk(const std::string& path, std::uint32_t& out_fid)
{
    // Split path into components
    std::vector<std::string> names;
    if (!path.empty() && path != "/")
    {
        std::istringstream stream(path);
        std::string component;
        while (std::getline(stream, component, '/'))
        {
            if (!component.empty())
            {
                names.push_back(component);
            }
        }
    }

    out_fid = fid_pool_.Allocate();
    const auto tag = NextTag();
    auto twalk = BuildTwalk(tag, root_fid_, out_fid, names);
    NinePMessage rwalk;
    auto rc = Transact(twalk, rwalk);
    if (rc != 0)
    {
        fid_pool_.Release(out_fid);
        return rc;
    }

    std::vector<Qid> qids;
    if (!ParseRwalk(rwalk, qids))
    {
        fid_pool_.Release(out_fid);
        return -EIO;
    }

    // Verify all path components were resolved
    if (qids.size() != names.size())
    {
        fid_pool_.Release(out_fid);
        return -ENOENT;
    }

    return 0;
}

std::int32_t NinePSession::Open(std::uint32_t fid, std::uint32_t flags, std::uint32_t& out_iounit)
{
    const auto tag = NextTag();
    auto tlopen = BuildTlopen(tag, fid, flags);
    NinePMessage rlopen;
    auto rc = Transact(tlopen, rlopen);
    if (rc != 0)
    {
        return rc;
    }

    Qid qid{};
    if (!ParseRlopen(rlopen, qid, out_iounit))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Read(std::uint32_t fid,
                                std::uint64_t offset,
                                std::uint32_t count,
                                std::vector<std::uint8_t>& out_data)
{
    const auto tag = NextTag();
    auto tread = BuildTread(tag, fid, offset, count);
    NinePMessage rread;
    auto rc = Transact(tread, rread);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRread(rread, out_data))
    {
        return -EIO;
    }

    return static_cast<std::int32_t>(out_data.size());
}

std::int32_t NinePSession::GetAttr(std::uint32_t fid, NinePStat& out_stat)
{
    const auto tag = NextTag();
    auto tgetattr = BuildTgetattr(tag, fid, kGetattrBasic);
    NinePMessage rgetattr;
    auto rc = Transact(tgetattr, rgetattr);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRgetattr(rgetattr, out_stat))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::ReadDir(std::uint32_t fid,
                                   std::uint64_t offset,
                                   std::uint32_t count,
                                   std::vector<DirEntry>& out_entries)
{
    const auto tag = NextTag();
    auto treaddir = BuildTreaddir(tag, fid, offset, count);
    NinePMessage rreaddir;
    auto rc = Transact(treaddir, rreaddir);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRreaddir(rreaddir, out_entries))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Clunk(std::uint32_t fid)
{
    const auto tag = NextTag();
    auto tclunk = BuildTclunk(tag, fid);
    NinePMessage rclunk;
    auto rc = Transact(tclunk, rclunk);
    fid_pool_.Release(fid);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRclunk(rclunk))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Write(std::uint32_t fid, std::uint64_t offset, const std::uint8_t* data, std::uint32_t count)
{
    const auto tag = NextTag();
    auto twrite = BuildTwrite(tag, fid, offset, data, count);
    NinePMessage rwrite;
    auto rc = Transact(twrite, rwrite);
    if (rc != 0)
    {
        return rc;
    }

    std::uint32_t written = 0U;
    if (!ParseRwrite(rwrite, written))
    {
        return -EIO;
    }

    return static_cast<std::int32_t>(written);
}

std::int32_t NinePSession::Create(std::uint32_t parent_fid,
                                  const std::string& name,
                                  std::uint32_t flags,
                                  std::uint32_t mode,
                                  std::uint32_t gid,
                                  Qid& out_qid,
                                  std::uint32_t& out_iounit)
{
    const auto tag = NextTag();
    auto tlcreate = BuildTlcreate(tag, parent_fid, name, flags, mode, gid);
    NinePMessage rlcreate;
    auto rc = Transact(tlcreate, rlcreate);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRlcreate(rlcreate, out_qid, out_iounit))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Mkdir(std::uint32_t parent_fid,
                                 const std::string& name,
                                 std::uint32_t mode,
                                 std::uint32_t gid)
{
    const auto tag = NextTag();
    auto tmkdir = BuildTmkdir(tag, parent_fid, name, mode, gid);
    NinePMessage rmkdir;
    auto rc = Transact(tmkdir, rmkdir);
    if (rc != 0)
    {
        return rc;
    }

    Qid qid{};
    if (!ParseRmkdir(rmkdir, qid))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Unlink(std::uint32_t dirfid, const std::string& name, std::uint32_t flags)
{
    const auto tag = NextTag();
    auto tunlinkat = BuildTunlinkat(tag, dirfid, name, flags);
    NinePMessage runlinkat;
    auto rc = Transact(tunlinkat, runlinkat);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRunlinkat(runlinkat))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::Rename(std::uint32_t olddirfid,
                                  const std::string& oldname,
                                  std::uint32_t newdirfid,
                                  const std::string& newname)
{
    const auto tag = NextTag();
    auto trenameat = BuildTrenameat(tag, olddirfid, oldname, newdirfid, newname);
    NinePMessage rrenameat;
    auto rc = Transact(trenameat, rrenameat);
    if (rc != 0)
    {
        return rc;
    }

    if (!ParseRrenameat(rrenameat))
    {
        return -EIO;
    }

    return 0;
}

std::int32_t NinePSession::WalkParent(const std::string& path, std::uint32_t& out_parent_fid, std::string& out_basename)
{
    // Split path into parent directory + basename
    auto pos = path.rfind('/');
    std::string parent_path;
    if (pos == std::string::npos)
    {
        parent_path = "";
        out_basename = path;
    }
    else
    {
        parent_path = path.substr(0, pos);
        out_basename = path.substr(pos + 1U);
    }

    if (out_basename.empty())
    {
        return -EINVAL;
    }

    return Walk(parent_path, out_parent_fid);
}

std::uint32_t NinePSession::GetRootFid() const
{
    return root_fid_;
}

std::uint32_t NinePSession::GetMessageSize() const
{
    return message_size_;
}

}  // namespace virtio9p
