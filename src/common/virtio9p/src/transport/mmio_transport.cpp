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
#include "transport/mmio_transport.h"
#include "log/log.h"
#include "protocol/nine_p_types.h"
#include "transport/virtio_defs.h"

#include <sys/mman.h>
#include <sys/neutrino.h>
#include <cerrno>
#include <cstring>
#include <ctime>

namespace virtio9p
{

MmioTransportImpl::MmioTransportImpl(const MmioConfig& config) : config_(config) {}

MmioTransportImpl::~MmioTransportImpl()
{
    Shutdown();
}

std::uint32_t MmioTransportImpl::ReadReg(std::uint32_t offset) const
{
    if (regs_ == nullptr)
    {
        return 0U;
    }
    return *reinterpret_cast<volatile const std::uint32_t*>(regs_ + offset);
}

void MmioTransportImpl::WriteReg(std::uint32_t offset, std::uint32_t value)
{
    if (regs_ == nullptr)
    {
        return;
    }
    *reinterpret_cast<volatile std::uint32_t*>(regs_ + offset) = value;
}

std::int32_t MmioTransportImpl::Initialize()
{
    // Map the MMIO register region
    regs_ = static_cast<volatile std::uint8_t*>(mmap_device_memory(
        nullptr, config_.region_size, PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, config_.base_address));
    if (regs_ == MAP_FAILED)
    {
        regs_ = nullptr;
        return -errno;
    }

    // Verify magic value
    const std::uint32_t magic = ReadReg(mmio::kMagicValue);
    if (magic != 0x74726976U)  // "virt" in little-endian
    {
        V9P_ERR("bad magic 0x%08x at 0x%lx", magic, config_.base_address);
        Shutdown();
        return -ENODEV;
    }

    // Verify device ID is 9 (9P transport)
    const std::uint32_t device_id = ReadReg(mmio::kDeviceId);
    if (device_id != kVirtio9pDeviceId)
    {
        V9P_ERR("unexpected device id %u (expected 9)", device_id);
        Shutdown();
        return -ENODEV;
    }

    // Negotiate features and set up virtqueue
    auto rc = NegotiateFeatures();
    if (rc != 0)
    {
        Shutdown();
        return rc;
    }

    rc = SetupVirtqueue();
    if (rc != 0)
    {
        Shutdown();
        return rc;
    }

    // Allocate DMA bounce buffers for 9P message exchange
    dma_req_virt_ = static_cast<std::uint8_t*>(mmap(
        nullptr, kMaxMessageSize, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_ANON | MAP_SHARED, NOFD, 0));
    if (dma_req_virt_ == MAP_FAILED)
    {
        dma_req_virt_ = nullptr;
        Shutdown();
        return -ENOMEM;
    }
    {
        off64_t off = 0;
        if (mem_offset64(dma_req_virt_, NOFD, kMaxMessageSize, &off, nullptr) != 0)
        {
            Shutdown();
            return -errno;
        }
        dma_req_phys_ = static_cast<std::uint64_t>(off);
    }

    dma_resp_virt_ = static_cast<std::uint8_t*>(mmap(
        nullptr, kMaxMessageSize, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_ANON | MAP_SHARED, NOFD, 0));
    if (dma_resp_virt_ == MAP_FAILED)
    {
        dma_resp_virt_ = nullptr;
        Shutdown();
        return -ENOMEM;
    }
    {
        off64_t off = 0;
        if (mem_offset64(dma_resp_virt_, NOFD, kMaxMessageSize, &off, nullptr) != 0)
        {
            Shutdown();
            return -errno;
        }
        dma_resp_phys_ = static_cast<std::uint64_t>(off);
    }

    // Set up IRQ for completion notification
    chid_ = ChannelCreate(0);
    if (chid_ == -1)
    {
        Shutdown();
        return -errno;
    }
    coid_ = ConnectAttach(0, 0, chid_, _NTO_SIDE_CHANNEL, 0);
    if (coid_ == -1)
    {
        Shutdown();
        return -errno;
    }

    SIGEV_PULSE_INIT(&irq_event_, coid_, SIGEV_PULSE_PRIO_INHERIT, 1, 0);
    irq_id_ = InterruptAttachEvent(static_cast<int>(config_.irq), &irq_event_, _NTO_INTR_FLAGS_TRK_MSK);
    if (irq_id_ == -1)
    {
        Shutdown();
        return -errno;
    }

    return 0;
}

std::int32_t MmioTransportImpl::NegotiateFeatures()
{
    // Read and store MMIO version
    mmio_version_ = ReadReg(mmio::kVersion);
    V9P_INFO("mmio version=%u", mmio_version_);

    // Reset device
    WriteReg(mmio::kStatus, 0U);

    // Acknowledge
    WriteReg(mmio::kStatus, status::kAcknowledge);

    // Driver loaded
    WriteReg(mmio::kStatus, status::kAcknowledge | status::kDriver);

    if (mmio_version_ == 1U)
    {
        // Legacy v1: GuestPageSize must be set before queue setup
        WriteReg(mmio::kGuestPageSize, 4096U);

        // Read device features (single page, no FeaturesSel in practice)
        WriteReg(mmio::kDeviceFeaturesSel, 0U);
        const std::uint32_t device_features = ReadReg(mmio::kDeviceFeatures);
        V9P_DBG("v1 device features=0x%08x", device_features);

        // Accept mount_tag feature (bit 0)
        WriteReg(mmio::kDriverFeaturesSel, 0U);
        WriteReg(mmio::kDriverFeatures, device_features & 0x1U);

        // For v1 there is no FEATURES_OK step — go straight to queue setup
    }
    else
    {
        // Modern v2: full feature negotiation with FEATURES_OK
        WriteReg(mmio::kDeviceFeaturesSel, 0U);
        const std::uint32_t device_features_lo = ReadReg(mmio::kDeviceFeatures);
        WriteReg(mmio::kDeviceFeaturesSel, 1U);
        const std::uint32_t device_features_hi = ReadReg(mmio::kDeviceFeatures);
        V9P_DBG("v2 device features lo=0x%08x hi=0x%08x", device_features_lo, device_features_hi);

        WriteReg(mmio::kDriverFeaturesSel, 0U);
        WriteReg(mmio::kDriverFeatures, device_features_lo & 0x1U);
        WriteReg(mmio::kDriverFeaturesSel, 1U);
        WriteReg(mmio::kDriverFeatures, device_features_hi & 0x1U);

        WriteReg(mmio::kStatus, status::kAcknowledge | status::kDriver | status::kFeaturesOk);
        const std::uint32_t s = ReadReg(mmio::kStatus);
        if ((s & status::kFeaturesOk) == 0U)
        {
            V9P_ERR("feature negotiation failed");
            WriteReg(mmio::kStatus, status::kFailed);
            return -EIO;
        }
    }

    return 0;
}

std::int32_t MmioTransportImpl::SetupVirtqueue()
{
    WriteReg(mmio::kQueueSel, 0U);

    const std::uint32_t max_size = ReadReg(mmio::kQueueNumMax);
    if (max_size == 0U)
    {
        return -EIO;
    }

    const std::uint16_t queue_size =
        (max_size < kDefaultQueueSize) ? static_cast<std::uint16_t>(max_size) : kDefaultQueueSize;

    virtqueue_ = std::make_unique<Virtqueue>(queue_size);

    if (mmio_version_ == 1U)
    {
        // Legacy v1: single contiguous allocation, activated via QueuePfn
        constexpr std::uint32_t kPageSize = 4096U;
        auto rc = virtqueue_->InitializeLegacy(kPageSize);
        if (rc != 0)
        {
            return rc;
        }

        WriteReg(mmio::kQueueNum, queue_size);
        WriteReg(mmio::kQueueAlign, kPageSize);

        const std::uint64_t base_phys = virtqueue_->GetDescAddr();
        const auto pfn = static_cast<std::uint32_t>(base_phys / kPageSize);

        V9P_DBG("v1 queue base_phys=0x%lx pfn=0x%x size=%u", static_cast<unsigned long>(base_phys), pfn, queue_size);

        // Setting QueuePfn to non-zero activates the queue in v1
        WriteReg(mmio::kQueuePfn, pfn);

        // Mark driver OK (no FEATURES_OK bit for v1)
        WriteReg(mmio::kStatus, status::kAcknowledge | status::kDriver | status::kDriverOk);
    }
    else
    {
        // Modern v2: separate allocations, activated via QueueReady
        auto rc = virtqueue_->Initialize();
        if (rc != 0)
        {
            return rc;
        }

        WriteReg(mmio::kQueueNum, queue_size);

        const std::uint64_t desc_addr = virtqueue_->GetDescAddr();
        const std::uint64_t avail_addr = virtqueue_->GetAvailAddr();
        const std::uint64_t used_addr = virtqueue_->GetUsedAddr();

        V9P_DBG("v2 queue desc=0x%lx avail=0x%lx used=0x%lx size=%u",
                static_cast<unsigned long>(desc_addr),
                static_cast<unsigned long>(avail_addr),
                static_cast<unsigned long>(used_addr),
                queue_size);

        WriteReg(mmio::kQueueDescLow, static_cast<std::uint32_t>(desc_addr));
        WriteReg(mmio::kQueueDescHigh, static_cast<std::uint32_t>(desc_addr >> 32U));
        WriteReg(mmio::kQueueAvailLow, static_cast<std::uint32_t>(avail_addr));
        WriteReg(mmio::kQueueAvailHigh, static_cast<std::uint32_t>(avail_addr >> 32U));
        WriteReg(mmio::kQueueUsedLow, static_cast<std::uint32_t>(used_addr));
        WriteReg(mmio::kQueueUsedHigh, static_cast<std::uint32_t>(used_addr >> 32U));

        WriteReg(mmio::kQueueReady, 1U);
        WriteReg(mmio::kStatus, status::kAcknowledge | status::kDriver | status::kFeaturesOk | status::kDriverOk);
    }

    return 0;
}

std::int32_t MmioTransportImpl::ResetDevice()
{
    V9P_WARN("MMIO device reset: reclaiming in-flight descriptors");

    // Reset the virtio device — this makes it drop all in-flight buffers.
    if (regs_ != nullptr)
    {
        WriteReg(mmio::kStatus, 0U);

        // Virtio spec §2.1.1: wait for device_status to read back 0.
        for (int i = 0; i < 100; ++i)
        {
            if (ReadReg(mmio::kStatus) == 0U)
            {
                break;
            }
            // Small delay — the device needs time to quiesce.
            struct timespec ts = {0, 10000000};  // 10 ms
            nanosleep(&ts, nullptr);
        }
    }
    virtqueue_.reset();

    // Detach and re-attach IRQ so no stale pulses remain.
    if (irq_id_ >= 0)
    {
        InterruptDetach(irq_id_);
        irq_id_ = -1;
    }
    if (coid_ >= 0)
    {
        ConnectDetach(coid_);
        coid_ = -1;
    }
    if (chid_ >= 0)
    {
        ChannelDestroy(chid_);
        chid_ = -1;
    }

    // Free old DMA bounce buffers.
    if (dma_req_virt_ != nullptr)
    {
        munmap(dma_req_virt_, kMaxMessageSize);
        dma_req_virt_ = nullptr;
    }
    if (dma_resp_virt_ != nullptr)
    {
        munmap(dma_resp_virt_, kMaxMessageSize);
        dma_resp_virt_ = nullptr;
    }

    // Re-initialize: negotiate features, set up virtqueue, allocate DMA, attach IRQ.
    auto rc = NegotiateFeatures();
    if (rc != 0)
    {
        return rc;
    }

    rc = SetupVirtqueue();
    if (rc != 0)
    {
        return rc;
    }

    dma_req_virt_ = static_cast<std::uint8_t*>(mmap(
        nullptr, kMaxMessageSize, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_ANON | MAP_SHARED, NOFD, 0));
    if (dma_req_virt_ == MAP_FAILED)
    {
        dma_req_virt_ = nullptr;
        return -ENOMEM;
    }
    {
        off64_t off = 0;
        if (mem_offset64(dma_req_virt_, NOFD, kMaxMessageSize, &off, nullptr) != 0)
        {
            return -errno;
        }
        dma_req_phys_ = static_cast<std::uint64_t>(off);
    }

    dma_resp_virt_ = static_cast<std::uint8_t*>(mmap(
        nullptr, kMaxMessageSize, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_ANON | MAP_SHARED, NOFD, 0));
    if (dma_resp_virt_ == MAP_FAILED)
    {
        dma_resp_virt_ = nullptr;
        return -ENOMEM;
    }
    {
        off64_t off = 0;
        if (mem_offset64(dma_resp_virt_, NOFD, kMaxMessageSize, &off, nullptr) != 0)
        {
            return -errno;
        }
        dma_resp_phys_ = static_cast<std::uint64_t>(off);
    }

    chid_ = ChannelCreate(0);
    if (chid_ == -1)
    {
        return -errno;
    }
    coid_ = ConnectAttach(0, 0, chid_, _NTO_SIDE_CHANNEL, 0);
    if (coid_ == -1)
    {
        return -errno;
    }

    SIGEV_PULSE_INIT(&irq_event_, coid_, SIGEV_PULSE_PRIO_INHERIT, 1, 0);
    irq_id_ = InterruptAttachEvent(static_cast<int>(config_.irq), &irq_event_, _NTO_INTR_FLAGS_TRK_MSK);
    if (irq_id_ == -1)
    {
        return -errno;
    }

    return 0;
}

std::int32_t MmioTransportImpl::Exchange(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response)
{
    if ((virtqueue_ == nullptr) || (dma_req_virt_ == nullptr) || (dma_resp_virt_ == nullptr))
    {
        return -EINVAL;
    }

    // Zero the response buffer so stale data cannot leak across requests.
    std::memset(dma_resp_virt_, 0, kMaxMessageSize);

    // Copy request data to DMA bounce buffer
    std::memcpy(dma_req_virt_, request.data(), request.size());

    auto head =
        virtqueue_->AddBuf(dma_req_phys_, static_cast<std::uint32_t>(request.size()), dma_resp_phys_, kMaxMessageSize);
    if (head < 0)
    {
        V9P_ERR("AddBuf failed: %d", head);
        return head;
    }

    virtqueue_->Kick();
    WriteReg(mmio::kQueueNotify, 0U);

    std::uint32_t out_len = 0;
    auto rc = WaitForCompletion(out_len);
    if (rc < 0)
    {
        // Log which 9P message type timed out (type byte is at offset 4).
        if (request.size() > 4U)
        {
            V9P_ERR("MMIO Exchange failed (rc=%d) for 9P msg type %u", rc, static_cast<unsigned>(request[4]));
        }
        // Device may still hold our descriptors/buffers.  A full reset is the
        // only spec-compliant way to reclaim them before the next Exchange().
        (void)ResetDevice();
        return rc;
    }

    // Copy response from DMA bounce buffer
    response.resize(out_len);
    std::memcpy(response.data(), dma_resp_virt_, out_len);
    return 0;
}

std::int32_t MmioTransportImpl::WaitForCompletion(std::uint32_t& out_len)
{
    // Set a 30-second timeout for the IRQ pulse
    constexpr std::uint64_t kTimeoutNs = 30ULL * 1000ULL * 1000ULL * 1000ULL;
    struct _pulse pulse;
    std::uint64_t timeout_nsec = kTimeoutNs;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, nullptr, &timeout_nsec, nullptr);

    int rcvid = MsgReceivePulse(chid_, &pulse, sizeof(pulse), nullptr);
    if (rcvid == -1)
    {
        V9P_ERR("WaitForCompletion timeout/error: %s", strerror(errno));
        return -errno;
    }

    // Acknowledge the interrupt
    const std::uint32_t isr = ReadReg(mmio::kInterruptStatus);
    WriteReg(mmio::kInterruptAck, isr);
    InterruptUnmask(static_cast<int>(config_.irq), irq_id_);

    auto rc = virtqueue_->GetBuf(out_len);
    if (rc >= 0)
    {
        return 0;
    }

    // The interrupt fired but the used ring update may not yet be
    // visible to the CPU (DMA/KVM ordering race).  Brief spin-wait
    // before concluding failure.
    for (int spin = 0; spin < 100; ++spin)
    {
        struct timespec ts = {0, 100000};  // 100 µs
        nanosleep(&ts, nullptr);
        rc = virtqueue_->GetBuf(out_len);
        if (rc >= 0)
        {
            return 0;
        }
    }

    V9P_ERR("MMIO WaitForCompletion: used ring empty after interrupt");
    return -EIO;
}

std::int32_t MmioTransportImpl::GetMountTag(std::string& tag)
{
    if (regs_ == nullptr)
    {
        return -EINVAL;
    }

    // The mount tag is in the device-specific config space.
    // For virtio-9p: config[0..1] = tag_len (uint16), config[2..] = tag bytes
    const auto tag_len =
        static_cast<std::uint16_t>(*reinterpret_cast<volatile const std::uint16_t*>(regs_ + mmio::kConfig));

    if (tag_len == 0U || tag_len > 256U)
    {
        return -EINVAL;
    }

    tag.resize(tag_len);
    for (std::uint16_t i = 0U; i < tag_len; ++i)
    {
        tag[i] = static_cast<char>(*(regs_ + mmio::kConfig + 2U + i));
    }

    return 0;
}

void MmioTransportImpl::Shutdown()
{
    if (irq_id_ >= 0)
    {
        InterruptDetach(irq_id_);
        irq_id_ = -1;
    }
    if (coid_ >= 0)
    {
        ConnectDetach(coid_);
        coid_ = -1;
    }
    if (chid_ >= 0)
    {
        ChannelDestroy(chid_);
        chid_ = -1;
    }
    if (dma_req_virt_ != nullptr)
    {
        munmap(dma_req_virt_, kMaxMessageSize);
        dma_req_virt_ = nullptr;
    }
    if (dma_resp_virt_ != nullptr)
    {
        munmap(dma_resp_virt_, kMaxMessageSize);
        dma_resp_virt_ = nullptr;
    }
    if (regs_ != nullptr)
    {
        // Reset device before unmapping
        WriteReg(mmio::kStatus, 0U);
        munmap_device_memory(const_cast<std::uint8_t*>(const_cast<volatile std::uint8_t*>(regs_)), config_.region_size);
        regs_ = nullptr;
    }
    virtqueue_.reset();
}

}  // namespace virtio9p
