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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_PCI_TRANSPORT_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_PCI_TRANSPORT_H

#include "transport/transport.h"
#include "transport/virtqueue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace virtio9p
{

/// Virtio-9P transport over PCI (x86_64).
///
/// Discovers the virtio-9p PCI device (vendor 0x1AF4, device 0x1009),
/// maps its BARs, and communicates through a single virtqueue.
class PciTransportImpl final : public Transport
{
  public:
    PciTransportImpl() = default;
    ~PciTransportImpl() override;

    std::int32_t Initialize() override;
    std::int32_t Exchange(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response) override;
    std::int32_t GetMountTag(std::string& tag) override;
    std::int32_t ResetDevice() override;
    void Shutdown() override;

  private:
    /// Scan PCI bus for virtio-9p device.
    std::int32_t DiscoverDevice();

    /// Map PCI BARs and set up for MMIO access.
    std::int32_t MapBars();

    /// Initialize the virtio device via PCI config space.
    std::int32_t InitializeDevice();

    /// Wait for the device to consume a buffer.
    std::int32_t WaitForCompletion(std::uint32_t& out_len);

    /// Negotiate virtio features.
    std::int32_t NegotiateFeatures();

    /// Set up the virtqueue.
    std::int32_t SetupVirtqueue();

    /// Read a register from the common configuration space.
    std::uint8_t ReadCommon8(std::uint32_t offset) const;
    std::uint16_t ReadCommon16(std::uint32_t offset) const;
    std::uint32_t ReadCommon32(std::uint32_t offset) const;
    void WriteCommon8(std::uint32_t offset, std::uint8_t value);
    void WriteCommon16(std::uint32_t offset, std::uint16_t value);
    void WriteCommon32(std::uint32_t offset, std::uint32_t value);
    void WriteCommon64(std::uint32_t offset, std::uint64_t value);

    volatile std::uint8_t* common_cfg_{nullptr};
    volatile std::uint8_t* notify_base_{nullptr};
    volatile std::uint8_t* device_cfg_{nullptr};
    volatile std::uint8_t* isr_cfg_{nullptr};
    std::uint32_t notify_offset_multiplier_{0U};
    std::uint16_t queue_notify_off_{0U};
    std::unique_ptr<Virtqueue> virtqueue_{};
    void* pci_handle_{nullptr};
    std::uint32_t irq_num_{0U};
    bool use_msix_{false};
    std::uint32_t msix_cap_offset_{0U};
    volatile std::uint8_t* msix_table_{nullptr};
    std::uint64_t msix_table_size_{0U};
    int irq_id_{-1};
    int chid_{-1};
    int coid_{-1};
    struct sigevent irq_event_{};

    // DMA bounce buffers for 9P message exchange
    std::uint8_t* dma_req_virt_{nullptr};
    std::uint64_t dma_req_phys_{0U};
    std::uint8_t* dma_resp_virt_{nullptr};
    std::uint64_t dma_resp_phys_{0U};

    // Mapped BAR regions to unmap on shutdown
    volatile std::uint8_t* bar_virt_[6]{};
    std::uint64_t bar_size_[6]{};
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_PCI_TRANSPORT_H
