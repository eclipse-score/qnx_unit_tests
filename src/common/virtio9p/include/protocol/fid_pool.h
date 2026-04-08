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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_FID_POOL_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_FID_POOL_H

#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace virtio9p
{

/// Manages allocation and release of 9P fid values.
///
/// Fids are uint32 handles that the client assigns to reference server-side
/// file objects. This pool tracks which fids are in use and hands out the
/// next available one.
class FidPool final
{
  public:
    FidPool();
    ~FidPool() = default;

    FidPool(const FidPool&) = delete;
    FidPool& operator=(const FidPool&) = delete;
    FidPool(FidPool&&) = delete;
    FidPool& operator=(FidPool&&) = delete;

    /// Allocate the next available fid. Returns the fid value.
    std::uint32_t Allocate();

    /// Release a fid back to the pool.
    void Release(std::uint32_t fid);

    /// Check whether a fid is currently allocated.
    bool IsAllocated(std::uint32_t fid) const;

    /// Number of currently allocated fids.
    std::size_t Size() const;

    /// Reset the pool to its initial state (release all fids).
    void Reset();

  private:
    mutable std::mutex mutex_{};
    std::unordered_set<std::uint32_t> allocated_{};
    std::uint32_t next_fid_{0U};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_PROTOCOL_FID_POOL_H
