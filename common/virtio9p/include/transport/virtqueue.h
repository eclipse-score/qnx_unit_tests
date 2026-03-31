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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTQUEUE_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTQUEUE_H

#include "transport/virtio_defs.h"

#include <cstddef>
#include <cstdint>

namespace virtio9p
{

/// Manages a single virtqueue (split virtqueue layout) for virtio devices.
///
/// All ring memory is allocated as physically contiguous DMA-capable memory
/// so that the virtio device can access it via guest-physical addresses.
///
/// Two initialization modes are supported:
/// - Initialize(): Modern (v2) — three separate DMA allocations.
/// - InitializeLegacy(align): Legacy (v1) — single contiguous DMA block with
///   desc/avail/used at spec-defined offsets, padded to 'align' boundary.
class Virtqueue final
{
  public:
    explicit Virtqueue(std::uint16_t queue_size);
    ~Virtqueue();

    Virtqueue(const Virtqueue&) = delete;
    Virtqueue& operator=(const Virtqueue&) = delete;
    Virtqueue(Virtqueue&&) = delete;
    Virtqueue& operator=(Virtqueue&&) = delete;

    /// Modern (v2): allocate three separate DMA blocks for desc, avail, used.
    std::int32_t Initialize();

    /// Legacy (v1): allocate a single contiguous DMA block.
    /// @param align Alignment for the used ring (typically page size, 4096).
    /// @return 0 on success, negative errno on failure.
    std::int32_t InitializeLegacy(std::uint32_t align);

    /// Add a buffer chain (request + response) to the descriptor table.
    /// Buffer addresses must be guest-physical (DMA-accessible by device).
    std::int32_t AddBuf(std::uint64_t out_phys, std::uint32_t out_len, std::uint64_t in_phys, std::uint32_t in_len);

    /// Issue a memory barrier to ensure ring writes are visible before notify.
    void Kick();

    /// Check if the device has consumed any buffers. Non-blocking.
    std::int32_t GetBuf(std::uint32_t& out_len);

    /// Get the guest-physical address of the descriptor table.
    std::uint64_t GetDescAddr() const;

    /// Get the guest-physical address of the available ring.
    std::uint64_t GetAvailAddr() const;

    /// Get the guest-physical address of the used ring.
    std::uint64_t GetUsedAddr() const;

    /// Get the configured queue size.
    std::uint16_t GetQueueSize() const;

  private:
    /// Allocate physically contiguous DMA-capable memory.
    static void* AllocDma(std::size_t size, std::uint64_t& phys_addr);
    /// Free DMA memory.
    static void FreeDma(void* virt, std::size_t size);
    /// Initialize the descriptor free chain (common for both modes).
    void InitDescChain();

    std::uint16_t queue_size_;
    std::uint16_t free_head_{0U};
    std::uint16_t num_free_;
    std::uint16_t last_used_idx_{0U};
    bool legacy_mode_{false};

    // For modern: three separate allocations. For legacy: only base_ is used.
    void* base_virt_{nullptr};
    std::uint64_t base_phys_{0U};
    std::size_t base_alloc_size_{0U};

    // Descriptor table virtual pointer
    VringDesc* desc_virt_{nullptr};
    std::uint64_t desc_phys_{0U};

    // Available ring virtual pointer: [flags(u16), idx(u16), ring[queue_size](u16)]
    std::uint16_t* avail_virt_{nullptr};
    std::uint64_t avail_phys_{0U};

    // Used ring virtual pointer: [flags(u16), idx(u16), elem[queue_size](VringUsedElem)]
    std::uint8_t* used_virt_{nullptr};
    std::uint64_t used_phys_{0U};

    // Only used for modern mode (separate allocations to free)
    std::size_t desc_alloc_size_{0U};
    std::size_t avail_alloc_size_{0U};
    std::size_t used_alloc_size_{0U};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTQUEUE_H
