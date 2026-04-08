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
#include "transport/virtqueue.h"

#include <sys/mman.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace virtio9p
{

Virtqueue::Virtqueue(std::uint16_t queue_size) : queue_size_(queue_size), num_free_(queue_size) {}

Virtqueue::~Virtqueue()
{
    if (legacy_mode_)
    {
        FreeDma(base_virt_, base_alloc_size_);
    }
    else
    {
        if (desc_virt_ != nullptr)
        {
            FreeDma(desc_virt_, desc_alloc_size_);
        }
        if (avail_virt_ != nullptr)
        {
            FreeDma(avail_virt_, avail_alloc_size_);
        }
        if (used_virt_ != nullptr)
        {
            FreeDma(used_virt_, used_alloc_size_);
        }
    }
}

void* Virtqueue::AllocDma(std::size_t size, std::uint64_t& phys_addr)
{
    phys_addr = 0U;
    void* virt = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_ANON | MAP_SHARED, NOFD, 0);
    if (virt == MAP_FAILED)
    {
        return nullptr;
    }
    off64_t offset = 0;
    if (mem_offset64(virt, NOFD, size, &offset, nullptr) != 0)
    {
        munmap(virt, size);
        return nullptr;
    }
    phys_addr = static_cast<std::uint64_t>(offset);
    return virt;
}

void Virtqueue::FreeDma(void* virt, std::size_t size)
{
    if (virt == nullptr)
    {
        return;
    }
    munmap(virt, size);
}

void Virtqueue::InitDescChain()
{
    for (std::uint16_t i = 0U; i < queue_size_; ++i)
    {
        desc_virt_[i].next = i + 1U;
    }
    free_head_ = 0U;
    num_free_ = queue_size_;
    last_used_idx_ = 0U;
}

static std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

std::int32_t Virtqueue::InitializeLegacy(std::uint32_t align)
{
    legacy_mode_ = true;

    // Legacy virtqueue layout (single contiguous block):
    //   [desc_table: queue_size * 16]
    //   [avail_ring: 4 + queue_size * 2 + 2(used_event)]
    //   [padding to 'align' boundary]
    //   [used_ring:  4 + queue_size * 8 + 2(avail_event)]
    const std::size_t desc_size = static_cast<std::size_t>(queue_size_) * sizeof(VringDesc);
    const std::size_t avail_size = 4U + static_cast<std::size_t>(queue_size_) * 2U + 2U;
    const std::size_t used_offset = AlignUp(desc_size + avail_size, align);
    const std::size_t used_size = 4U + static_cast<std::size_t>(queue_size_) * sizeof(VringUsedElem) + 2U;
    base_alloc_size_ = used_offset + used_size;

    base_virt_ = AllocDma(base_alloc_size_, base_phys_);
    if (base_virt_ == nullptr)
    {
        return -ENOMEM;
    }
    std::memset(base_virt_, 0, base_alloc_size_);

    auto* base = static_cast<std::uint8_t*>(base_virt_);
    desc_virt_ = reinterpret_cast<VringDesc*>(base);
    desc_phys_ = base_phys_;

    avail_virt_ = reinterpret_cast<std::uint16_t*>(base + desc_size);
    avail_phys_ = base_phys_ + desc_size;

    used_virt_ = base + used_offset;
    used_phys_ = base_phys_ + used_offset;

    InitDescChain();
    return 0;
}

std::int32_t Virtqueue::Initialize()
{
    legacy_mode_ = false;

    // Descriptor table
    desc_alloc_size_ = static_cast<std::size_t>(queue_size_) * sizeof(VringDesc);
    desc_virt_ = static_cast<VringDesc*>(AllocDma(desc_alloc_size_, desc_phys_));
    if (desc_virt_ == nullptr)
    {
        return -ENOMEM;
    }
    std::memset(desc_virt_, 0, desc_alloc_size_);

    // Available ring
    avail_alloc_size_ = 4U + static_cast<std::size_t>(queue_size_) * 2U;
    avail_virt_ = static_cast<std::uint16_t*>(AllocDma(avail_alloc_size_, avail_phys_));
    if (avail_virt_ == nullptr)
    {
        FreeDma(desc_virt_, desc_alloc_size_);
        desc_virt_ = nullptr;
        return -ENOMEM;
    }
    std::memset(avail_virt_, 0, avail_alloc_size_);

    // Used ring
    used_alloc_size_ = 4U + static_cast<std::size_t>(queue_size_) * sizeof(VringUsedElem);
    used_virt_ = static_cast<std::uint8_t*>(AllocDma(used_alloc_size_, used_phys_));
    if (used_virt_ == nullptr)
    {
        FreeDma(avail_virt_, avail_alloc_size_);
        avail_virt_ = nullptr;
        FreeDma(desc_virt_, desc_alloc_size_);
        desc_virt_ = nullptr;
        return -ENOMEM;
    }
    std::memset(used_virt_, 0, used_alloc_size_);

    InitDescChain();
    return 0;
}

std::int32_t Virtqueue::AddBuf(std::uint64_t out_phys,
                               std::uint32_t out_len,
                               std::uint64_t in_phys,
                               std::uint32_t in_len)
{
    // Need at least 2 descriptors (one for request, one for response)
    if (num_free_ < 2U)
    {
        return -ENOSPC;
    }

    const std::uint16_t head = free_head_;

    // First descriptor: device-readable (request)
    std::uint16_t idx = free_head_;
    desc_virt_[idx].addr = out_phys;
    desc_virt_[idx].len = out_len;
    desc_virt_[idx].flags = kVringDescFlagNext;
    free_head_ = desc_virt_[idx].next;
    --num_free_;

    // Second descriptor: device-writable (response)
    const std::uint16_t prev = idx;
    idx = free_head_;
    desc_virt_[idx].addr = in_phys;
    desc_virt_[idx].len = in_len;
    desc_virt_[idx].flags = kVringDescFlagWrite;
    desc_virt_[prev].next = idx;
    free_head_ = desc_virt_[idx].next;
    --num_free_;

    // Add head to available ring
    // avail_virt_ layout: [0]=flags, [1]=idx, [2..]=ring entries
    const std::uint16_t avail_idx = avail_virt_[1];
    avail_virt_[2U + (avail_idx % queue_size_)] = head;

    // Full memory barrier to ensure descriptor writes are visible before idx update
    __sync_synchronize();
    avail_virt_[1] = avail_idx + 1U;

    return static_cast<std::int32_t>(head);
}

void Virtqueue::Kick()
{
    // Full memory barrier to ensure all ring writes are visible before notify
    __sync_synchronize();
}

std::int32_t Virtqueue::GetBuf(std::uint32_t& out_len)
{
    // Full memory barrier to ensure we see latest device writes
    __sync_synchronize();

    // Read device-written used_idx from the used ring header (offset 2)
    const auto device_used_idx = *reinterpret_cast<volatile const std::uint16_t*>(used_virt_ + 2U);

    if (last_used_idx_ == device_used_idx)
    {
        return -1;
    }

    // Read used element from the used ring body (offset 4+)
    const std::size_t elem_offset = 4U + static_cast<std::size_t>(last_used_idx_ % queue_size_) * sizeof(VringUsedElem);
    const auto* elem = reinterpret_cast<const VringUsedElem*>(used_virt_ + elem_offset);
    out_len = elem->len;
    const auto head = static_cast<std::int32_t>(elem->id);

    // Return descriptors to free list
    std::uint16_t i = static_cast<std::uint16_t>(elem->id);
    while (true)
    {
        const bool has_next = (desc_virt_[i].flags & kVringDescFlagNext) != 0U;
        const std::uint16_t next = desc_virt_[i].next;
        desc_virt_[i].next = free_head_;
        free_head_ = i;
        ++num_free_;
        if (!has_next)
        {
            break;
        }
        i = next;
    }

    ++last_used_idx_;
    return head;
}

std::uint64_t Virtqueue::GetDescAddr() const
{
    return desc_phys_;
}

std::uint64_t Virtqueue::GetAvailAddr() const
{
    return avail_phys_;
}

std::uint64_t Virtqueue::GetUsedAddr() const
{
    return used_phys_;
}

std::uint16_t Virtqueue::GetQueueSize() const
{
    return queue_size_;
}

}  // namespace virtio9p
