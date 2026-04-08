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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_MMIO_TRANSPORT_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_MMIO_TRANSPORT_H

#include "transport/transport.h"
#include "transport/virtqueue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace virtio9p
{

/// Configuration for MMIO-based virtio transport (ARM64).
struct MmioConfig
{
    std::uint64_t base_address{0U};
    std::uint32_t irq{0U};
    std::uint32_t region_size{0x200U};
};

/// Virtio-9P transport over MMIO (memory-mapped I/O).
///
/// Used on ARM64 where QEMU's virt machine exposes virtio devices
/// at fixed MMIO addresses. The driver maps the device registers via
/// mmap_device_memory() and communicates through a single virtqueue.
class MmioTransportImpl final : public Transport
{
  public:
    explicit MmioTransportImpl(const MmioConfig& config);
    ~MmioTransportImpl() override;

    std::int32_t Initialize() override;
    std::int32_t Exchange(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response) override;
    std::int32_t GetMountTag(std::string& tag) override;
    std::int32_t ResetDevice() override;
    void Shutdown() override;

  private:
    /// Read a 32-bit register from the MMIO region.
    std::uint32_t ReadReg(std::uint32_t offset) const;

    /// Write a 32-bit register to the MMIO region.
    void WriteReg(std::uint32_t offset, std::uint32_t value);

    /// Perform the virtio device initialization sequence.
    std::int32_t NegotiateFeatures();

    /// Set up the virtqueue in the device.
    std::int32_t SetupVirtqueue();

    /// Wait for the device to consume a buffer (poll or IRQ).
    std::int32_t WaitForCompletion(std::uint32_t& out_len);

    MmioConfig config_{};
    volatile std::uint8_t* regs_{nullptr};
    std::unique_ptr<Virtqueue> virtqueue_{};
    std::uint32_t mmio_version_{0U};
    int irq_id_{-1};
    int chid_{-1};
    int coid_{-1};
    struct sigevent irq_event_{};

    // DMA bounce buffers for 9P message exchange
    std::uint8_t* dma_req_virt_{nullptr};
    std::uint64_t dma_req_phys_{0U};
    std::uint8_t* dma_resp_virt_{nullptr};
    std::uint64_t dma_resp_phys_{0U};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_MMIO_TRANSPORT_H
