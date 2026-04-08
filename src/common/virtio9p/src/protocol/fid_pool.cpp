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
#include "protocol/fid_pool.h"

namespace virtio9p
{

FidPool::FidPool() = default;

std::uint32_t FidPool::Allocate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t fid = next_fid_++;
    allocated_.insert(fid);
    return fid;
}

void FidPool::Release(std::uint32_t fid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    allocated_.erase(fid);
}

bool FidPool::IsAllocated(std::uint32_t fid) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return allocated_.count(fid) > 0U;
}

std::size_t FidPool::Size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return allocated_.size();
}

void FidPool::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    allocated_.clear();
    next_fid_ = 0U;
}

}  // namespace virtio9p
