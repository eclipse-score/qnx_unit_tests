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
#include "transport/pci_transport.h"
#include "log/log.h"
#include "protocol/nine_p_types.h"
#include "transport/virtio_defs.h"

#include <cerrno>
#include <cstring>
#include <ctime>

extern "C" {
#include <pci/pci.h>
}
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/rsrcdbmgr.h>

namespace virtio9p
{

PciTransportImpl::~PciTransportImpl()
{
    Shutdown();
}

// --- Common config register helpers ---

std::uint8_t PciTransportImpl::ReadCommon8(std::uint32_t offset) const
{
    return *reinterpret_cast<volatile const std::uint8_t*>(common_cfg_ + offset);
}

std::uint16_t PciTransportImpl::ReadCommon16(std::uint32_t offset) const
{
    return *reinterpret_cast<volatile const std::uint16_t*>(common_cfg_ + offset);
}

std::uint32_t PciTransportImpl::ReadCommon32(std::uint32_t offset) const
{
    return *reinterpret_cast<volatile const std::uint32_t*>(common_cfg_ + offset);
}

void PciTransportImpl::WriteCommon8(std::uint32_t offset, std::uint8_t value)
{
    *reinterpret_cast<volatile std::uint8_t*>(common_cfg_ + offset) = value;
}

void PciTransportImpl::WriteCommon16(std::uint32_t offset, std::uint16_t value)
{
    *reinterpret_cast<volatile std::uint16_t*>(common_cfg_ + offset) = value;
}

void PciTransportImpl::WriteCommon32(std::uint32_t offset, std::uint32_t value)
{
    *reinterpret_cast<volatile std::uint32_t*>(common_cfg_ + offset) = value;
}

void PciTransportImpl::WriteCommon64(std::uint32_t offset, std::uint64_t value)
{
    *reinterpret_cast<volatile std::uint32_t*>(common_cfg_ + offset) = static_cast<std::uint32_t>(value);
    *reinterpret_cast<volatile std::uint32_t*>(common_cfg_ + offset + 4U) = static_cast<std::uint32_t>(value >> 32U);
}

// --- Initialization ---

std::int32_t PciTransportImpl::Initialize()
{
    auto rc = DiscoverDevice();
    if (rc != 0)
    {
        return rc;
    }

    rc = MapBars();
    if (rc != 0)
    {
        Shutdown();
        return rc;
    }

    rc = InitializeDevice();
    if (rc != 0)
    {
        Shutdown();
        return rc;
    }

    return 0;
}

std::int32_t PciTransportImpl::DiscoverDevice()
{
    const pci_bdf_t bdf = pci_device_find(0U, kVirtioPciVendorId, kVirtio9pPciDeviceId, PCI_CCODE_ANY);
    if (bdf == PCI_BDF_NONE)
    {
        V9P_ERR("virtio-9p PCI device not found");
        return -ENODEV;
    }

    V9P_INFO("found virtio-9p PCI device at BDF 0x%x", bdf);

    // Walk virtio PCI capabilities by reading PCI config space directly.
    // The QNX pci_device_read_cap() + cap_vend module approach fails with
    // PCI_ERR_NO_MODULE when the vendor capability module cannot be loaded.
    // Instead, we scan config space using pci_device_cfg_rd32().
    //
    // The capability linked list on this device is linked in reverse order
    // (last cap has next=0), and we cannot read the capabilities pointer at
    // 0x34 (pci_device_cfg_rd* only supports offsets >= 0x40). So we do a
    // linear scan of all dwords looking for vendor-specific caps (id=0x09).

    struct CapInfo
    {
        std::uint8_t bar;
        std::uint32_t offset;
        std::uint32_t length;
    };
    CapInfo common_info{};
    CapInfo notify_info{};
    CapInfo device_info{};
    CapInfo isr_info{};
    bool found_common = false;
    bool found_notify = false;
    bool found_device = false;
    bool found_isr = false;

    // Scan every 4-byte-aligned offset in the device-dependent config space
    // region for PCI capabilities.  We look for:
    //   0x09 — vendor-specific (virtio PCI capability structures)
    //   0x11 — MSI-X capability
    // We use 32-bit reads only, since pci_device_cfg_rd8 at non-aligned
    // offsets may silently return 0 on QNX.
    constexpr uint_t kCapRegionStart = 0x40U;
    constexpr uint_t kCapRegionEnd = 0xFCU;
    constexpr uint8_t kCapIdVendor = 0x09U;
    constexpr uint8_t kCapIdMsix = 0x11U;

    for (uint_t offset = kCapRegionStart; offset <= kCapRegionEnd; offset += 4U)
    {
        uint32_t hdr0 = 0U;
        if (pci_device_cfg_rd32(bdf, offset, &hdr0) != PCI_ERR_OK)
        {
            continue;
        }

        const auto cap_id = static_cast<uint8_t>(hdr0 & 0xFFU);

        if (cap_id == kCapIdMsix)
        {
            msix_cap_offset_ = offset;
            V9P_DBG("MSI-X PCI cap at 0x%x", offset);
            continue;
        }

        if (cap_id != kCapIdVendor)
        {
            continue;
        }

        // Virtio PCI capability layout (little-endian dwords from offset):
        //   dword +0:  [cap_vndr(0x09), cap_next, cap_len, cfg_type]
        //   dword +4:  [bar, pad, pad, pad]
        //   dword +8:  bar_offset (le32)
        //   dword +12: bar_length (le32)
        //   dword +16: notify_off_multiplier (le32, for notify caps with cap_len>=20)
        const auto cap_len = static_cast<uint8_t>((hdr0 >> 16U) & 0xFFU);
        const auto cfg_type = static_cast<uint8_t>((hdr0 >> 24U) & 0xFFU);

        // Validate: cfg_type should be 1-5 for virtio, cap_len >= 16
        if (cfg_type < 1U || cfg_type > 5U || cap_len < 16U)
        {
            continue;
        }

        uint32_t dword1 = 0U;
        pci_device_cfg_rd32(bdf, offset + 4U, &dword1);
        const auto bar = static_cast<uint8_t>(dword1 & 0xFFU);

        uint32_t bar_offset = 0U;
        uint32_t bar_length = 0U;
        pci_device_cfg_rd32(bdf, offset + 8U, &bar_offset);
        pci_device_cfg_rd32(bdf, offset + 12U, &bar_length);

        V9P_DBG("virtio PCI cap at 0x%x type=%u bar=%u offset=0x%x length=0x%x",
                offset,
                cfg_type,
                bar,
                bar_offset,
                bar_length);

        if (cfg_type == pci_cap::kCommonCfg)
        {
            common_info = {bar, bar_offset, bar_length};
            found_common = true;
        }
        else if (cfg_type == pci_cap::kNotifyCfg)
        {
            notify_info = {bar, bar_offset, bar_length};
            found_notify = true;
            if (cap_len >= 20U)
            {
                uint32_t mult = 0U;
                pci_device_cfg_rd32(bdf, offset + 16U, &mult);
                notify_offset_multiplier_ = mult;
            }
        }
        else if (cfg_type == pci_cap::kDeviceCfg)
        {
            device_info = {bar, bar_offset, bar_length};
            found_device = true;
        }
        else if (cfg_type == pci_cap::kIsrCfg)
        {
            isr_info = {bar, bar_offset, bar_length};
            found_isr = true;
        }

        // Skip past this capability's data to avoid re-parsing interior dwords
        const uint_t cap_dwords = (static_cast<uint_t>(cap_len) + 3U) / 4U;
        if (cap_dwords > 1U)
        {
            offset += (cap_dwords - 1U) * 4U;
        }
    }

    if (!found_common || !found_notify)
    {
        V9P_ERR(
            "missing required virtio PCI capabilities "
            "(common=%d notify=%d device=%d)",
            found_common ? 1 : 0,
            found_notify ? 1 : 0,
            found_device ? 1 : 0);
        return -ENODEV;
    }

    // Attach to device after reading capabilities
    pci_err_t pci_status = PCI_ERR_OK;
    pci_handle_ = reinterpret_cast<void*>(pci_device_attach(bdf, pci_attachFlags_e_OWNER, &pci_status));
    if (pci_status != PCI_ERR_OK)
    {
        V9P_ERR("pci_device_attach failed: %s", pci_strerror(pci_status));
        pci_handle_ = nullptr;
        return -EIO;
    }

    // Enable PCI Bus Master and Memory Space access.
    // pci_device_attach does NOT enable bus mastering; without it the
    // device cannot DMA into the virtqueue or bounce buffers.
    const auto hdl = reinterpret_cast<pci_devhdl_t>(pci_handle_);
    {
        pci_cmd_t cmd = 0U;
        if (pci_device_read_cmd(bdf, &cmd) == PCI_ERR_OK)
        {
            constexpr pci_cmd_t kNeeded = 0x06U;  // Memory Space (bit 1) + Bus Master (bit 2)
            if ((cmd & kNeeded) != kNeeded)
            {
                pci_cmd_t after = 0U;
                const pci_cmd_t new_cmd = cmd | kNeeded;
                if (pci_device_write_cmd(hdl, new_cmd, &after) != PCI_ERR_OK)
                {
                    V9P_ERR("pci_device_write_cmd failed");
                    return -EIO;
                }
                V9P_DBG("PCI command: 0x%04x -> 0x%04x", static_cast<unsigned>(cmd), static_cast<unsigned>(after));
            }
            else
            {
                V9P_INFO("PCI command: 0x%04x (bus master already set)", static_cast<unsigned>(cmd));
            }
        }
        else
        {
            V9P_ERR("warning: pci_device_read_cmd failed");
        }
    }

    // --- MSI-X setup: direct PCI register programming ---
    // With DO_BUS_CONFIG=no the pci-server doesn't allocate MSI-X
    // vectors, so pci_device_cfg_cap_enable fails.  Instead we
    // directly program the MSI-X PCI capability and table via MMIO.
    uint8_t msix_table_bar = 0U;
    uint32_t msix_table_offset = 0U;

    if (msix_cap_offset_ != 0U)
    {
        // Read MSI-X Message Control (cap_offset + 2, in upper 16 bits of dword)
        uint32_t msix_hdr = 0U;
        pci_device_cfg_rd32(bdf, msix_cap_offset_, &msix_hdr);
        const uint16_t msg_ctrl = static_cast<uint16_t>((msix_hdr >> 16U) & 0xFFFFU);
        const uint16_t table_size = (msg_ctrl & 0x07FFU) + 1U;

        // Read Table BIR + Offset (cap_offset + 4)
        uint32_t table_bir_off = 0U;
        pci_device_cfg_rd32(bdf, msix_cap_offset_ + 4U, &table_bir_off);
        msix_table_bar = static_cast<uint8_t>(table_bir_off & 0x07U);
        msix_table_offset = table_bir_off & ~0x07U;

        V9P_DBG(
            "MSI-X msg_ctrl=0x%04x table_size=%u "
            "table_bar=%u table_offset=0x%x",
            msg_ctrl,
            table_size,
            msix_table_bar,
            msix_table_offset);

        // Allocate an MSI-X interrupt.
        //
        // From the boot log, the kernel reports:
        //   MSI interrupt = 0x100 (base IRQ for MSI vectors)
        //   MSI vector no = 78   (base APIC vector)
        //   MSI vec count = 177
        //
        // To use MSI IRQ N (where N >= msi_interrupt_base):
        //   APIC vector = msi_vector_no + (N - msi_interrupt_base)
        //   IRQ number to use with InterruptAttachEvent = N
        //   Message Data for MSI-X table = APIC vector
        //
        // We pick the first MSI vector (offset 0).
        constexpr std::uint32_t kMsiInterruptBase = 0x100U;
        constexpr std::uint32_t kMsiVectorBase = 78U;
        constexpr std::uint32_t kMsiIrq = kMsiInterruptBase;  // offset 0
        (void)kMsiVectorBase;                                 // used later in MSI-X table programming

        // Create and claim the IRQ resource
        rsrc_alloc_t irq_create{};
        irq_create.start = kMsiIrq;
        irq_create.end = kMsiIrq;
        irq_create.flags = RSRCDBMGR_IRQ;
        if (rsrcdbmgr_create(&irq_create, 1) != EOK)
        {
            V9P_ERR("rsrcdbmgr_create IRQ %u failed: %s", kMsiIrq, strerror(errno));
        }

        rsrc_request_t irq_req{};
        irq_req.length = 1U;
        irq_req.start = kMsiIrq;
        irq_req.end = kMsiIrq;
        irq_req.flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RANGE;
        if (rsrcdbmgr_attach(&irq_req, 1) == EOK)
        {
            irq_num_ = static_cast<std::uint32_t>(irq_req.start);
            use_msix_ = true;
            V9P_DBG("allocated IRQ %u for MSI-X", irq_num_);
        }
        else
        {
            V9P_WARN(
                "rsrcdbmgr_attach IRQ failed: %s "
                "(falling back to INTx)",
                strerror(errno));
        }
    }

    // If MSI-X setup didn't succeed, read the legacy INTx IRQ
    if (!use_msix_)
    {
        pci_irq_t irqs[4]{};
        int_t nirq = 1;
        if (pci_device_read_irq(hdl, &nirq, irqs) != PCI_ERR_OK || nirq < 1)
        {
            V9P_ERR("pci_device_read_irq failed, nirq=%d", nirq);
            return -EIO;
        }
        irq_num_ = static_cast<std::uint32_t>(irqs[0]);
    }
    V9P_DBG("PCI IRQ=%u (%s)", irq_num_, use_msix_ ? "MSI-X" : "INTx");

    // Read BARs using pci_device_read_ba
    constexpr int kMaxBars = 6;
    pci_ba_t bars[kMaxBars]{};
    int_t nba = kMaxBars;
    if (pci_device_read_ba(hdl, &nba, bars, pci_reqType_e_UNSPECIFIED) != PCI_ERR_OK)
    {
        V9P_ERR("pci_device_read_ba failed");
        return -EIO;
    }

    V9P_DBG("PCI device has %d BARs", nba);

    // Debug: print all BARs
    for (int_t i = 0; i < nba; ++i)
    {
        V9P_DBG("BAR[%d] num=%u addr=0x%lx size=0x%lx type=%u",
                i,
                bars[i].bar_num,
                static_cast<unsigned long>(bars[i].addr),
                static_cast<unsigned long>(bars[i].size),
                bars[i].type);
    }

    // Map required BARs
    for (int_t i = 0; i < nba; ++i)
    {
        const auto bar_num = static_cast<std::uint8_t>(bars[i].bar_num);
        if (bar_num >= kMaxBars)
        {
            continue;
        }
        if (bar_virt_[bar_num] != nullptr)
        {
            continue;
        }

        const bool need_this_bar = (bar_num == common_info.bar) || (bar_num == notify_info.bar) ||
                                   (found_device && bar_num == device_info.bar) ||
                                   (found_isr && bar_num == isr_info.bar) || (use_msix_ && bar_num == msix_table_bar);

        if (!need_this_bar)
        {
            continue;
        }

        // With DO_BUS_CONFIG=no the pci-server may report size=0 for
        // BARs it didn't size (e.g. the MSI-X table BAR).  Use at
        // least one page so mmap_device_memory succeeds.
        auto map_size = static_cast<std::size_t>(bars[i].size);
        if (map_size == 0U)
        {
            map_size = 4096U;
        }

        auto* mapped = static_cast<volatile std::uint8_t*>(mmap_device_memory(
            nullptr, map_size, PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, static_cast<std::uint64_t>(bars[i].addr)));
        if (mapped == MAP_FAILED)
        {
            V9P_ERR("mmap BAR%u failed: %s", bar_num, strerror(errno));
            return -errno;
        }

        bar_virt_[bar_num] = mapped;
        bar_size_[bar_num] = map_size;
        V9P_DBG("mapped BAR%u phys=0x%lx size=0x%lx",
                bar_num,
                static_cast<unsigned long>(bars[i].addr),
                static_cast<unsigned long>(bars[i].size));
    }

    // Set config pointers
    if (bar_virt_[common_info.bar] == nullptr)
    {
        return -ENODEV;
    }
    common_cfg_ = bar_virt_[common_info.bar] + common_info.offset;

    if (bar_virt_[notify_info.bar] == nullptr)
    {
        return -ENODEV;
    }
    notify_base_ = bar_virt_[notify_info.bar] + notify_info.offset;

    if (found_device && bar_virt_[device_info.bar] != nullptr)
    {
        device_cfg_ = bar_virt_[device_info.bar] + device_info.offset;
    }

    if (found_isr && bar_virt_[isr_info.bar] != nullptr)
    {
        isr_cfg_ = bar_virt_[isr_info.bar] + isr_info.offset;
    }

    // --- Program MSI-X table and enable MSI-X in PCI config ---
    if (use_msix_ && bar_virt_[msix_table_bar] != nullptr)
    {
        volatile auto* table_entry = bar_virt_[msix_table_bar] + msix_table_offset;

        // MSI-X table entry 0 layout (each entry = 16 bytes):
        //   +0:  Message Address Low  (32 bits)
        //   +4:  Message Address High (32 bits)
        //   +8:  Message Data         (32 bits)
        //   +12: Vector Control       (32 bits, bit 0 = mask)
        //
        // x86 APIC MSI address format: 0xFEEnnnnn
        //   bits [19:12] = destination APIC ID (0 = BSP)
        //   bit  3       = Redirection Hint (0 = no)
        //   bit  2       = Destination Mode (0 = physical)
        // Message Data: bits [7:0] = APIC vector
        //   The APIC vector for our MSI IRQ is stored at offset 0
        //   from the kernel's MSI vector base (78), i.e. vector 78.
        constexpr uint32_t kApicMsiBase = 0xFEE00000U;
        constexpr uint32_t kMsiVectorNo = 78U;
        const uint32_t apic_vector = kMsiVectorNo + (irq_num_ - 0x100U);

        // Mask entry first
        *reinterpret_cast<volatile uint32_t*>(table_entry + 12U) = 1U;

        // Program address/data
        *reinterpret_cast<volatile uint32_t*>(table_entry + 0U) = kApicMsiBase;
        *reinterpret_cast<volatile uint32_t*>(table_entry + 4U) = 0U;
        *reinterpret_cast<volatile uint32_t*>(table_entry + 8U) = apic_vector;

        // Unmask entry
        *reinterpret_cast<volatile uint32_t*>(table_entry + 12U) = 0U;

        msix_table_ = table_entry;
        msix_table_size_ = 16U;

        // Enable MSI-X and set Function Mask in PCI Message Control.
        // Message Control is at cap_offset + 2 (16-bit).
        // bit 15 = MSI-X Enable, bit 14 = Function Mask
        // First enable with function mask set, then clear it.
        uint16_t new_msg_ctrl = 0U;
        pci_device_cfg_wr16(hdl, msix_cap_offset_ + 2U, 0xC000U, &new_msg_ctrl);  // Enable + FMask
        pci_device_cfg_wr16(hdl, msix_cap_offset_ + 2U, 0x8000U, &new_msg_ctrl);  // Enable, clear FMask

        // Set INTx Disable bit in PCI Command register (bit 10 = 0x0400)
        pci_cmd_t cmd = 0U;
        pci_device_read_cmd(bdf, &cmd);
        if ((cmd & 0x0400U) == 0U)
        {
            pci_cmd_t after = 0U;
            pci_device_write_cmd(hdl, cmd | 0x0400U, &after);
        }

        V9P_INFO(
            "MSI-X enabled: table entry 0 → "
            "addr=0x%08x data=0x%02x (APIC vec %u, IRQ %u)",
            kApicMsiBase,
            apic_vector,
            apic_vector,
            irq_num_);
    }
    else if (use_msix_)
    {
        // MSI-X table BAR didn't map — fall back to INTx
        V9P_WARN(
            "MSI-X table BAR%u not mapped, "
            "falling back to INTx",
            msix_table_bar);
        // Release the allocated IRQ
        rsrc_request_t irq_rel{};
        irq_rel.length = 1U;
        irq_rel.start = irq_num_;
        irq_rel.end = irq_num_;
        irq_rel.flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RANGE;
        rsrcdbmgr_detach(&irq_rel, 1);
        use_msix_ = false;

        // Read INTx IRQ
        pci_irq_t irqs[4]{};
        int_t nirq = 1;
        if (pci_device_read_irq(hdl, &nirq, irqs) != PCI_ERR_OK || nirq < 1)
        {
            return -EIO;
        }
        irq_num_ = static_cast<std::uint32_t>(irqs[0]);
    }

    return 0;
}

std::int32_t PciTransportImpl::MapBars()
{
    // BAR mapping is done in DiscoverDevice along with capability parsing
    return 0;
}

std::int32_t PciTransportImpl::InitializeDevice()
{
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

    // Allocate DMA bounce buffers
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

    // Set up IRQ for completion notification
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
    irq_id_ = InterruptAttachEvent(static_cast<int>(irq_num_), &irq_event_, _NTO_INTR_FLAGS_TRK_MSK);
    if (irq_id_ == -1)
    {
        V9P_ERR("InterruptAttachEvent IRQ %u failed: %s", irq_num_, strerror(errno));
        return -errno;
    }

    return 0;
}

std::int32_t PciTransportImpl::NegotiateFeatures()
{
    if (common_cfg_ == nullptr)
    {
        return -EINVAL;
    }

    // Reset device
    WriteCommon8(pci_common::kDeviceStatus, 0U);

    // Acknowledge
    WriteCommon8(pci_common::kDeviceStatus, static_cast<std::uint8_t>(status::kAcknowledge));

    // Driver loaded
    WriteCommon8(pci_common::kDeviceStatus, static_cast<std::uint8_t>(status::kAcknowledge | status::kDriver));

    // Read device features (low 32 bits)
    WriteCommon32(pci_common::kDeviceFeatureSelect, 0U);
    const std::uint32_t device_features_lo = ReadCommon32(pci_common::kDeviceFeature);

    // Accept mount_tag feature (bit 0)
    WriteCommon32(pci_common::kDriverFeatureSelect, 0U);
    WriteCommon32(pci_common::kDriverFeature, device_features_lo & 0x1U);

    // High 32 bits — accept version 1 if offered
    WriteCommon32(pci_common::kDeviceFeatureSelect, 1U);
    const std::uint32_t device_features_hi = ReadCommon32(pci_common::kDeviceFeature);
    WriteCommon32(pci_common::kDriverFeatureSelect, 1U);
    WriteCommon32(pci_common::kDriverFeature, device_features_hi & 0x1U);

    V9P_DBG("PCI features lo=0x%08x hi=0x%08x", device_features_lo, device_features_hi);

    // Set FEATURES_OK
    WriteCommon8(pci_common::kDeviceStatus,
                 static_cast<std::uint8_t>(status::kAcknowledge | status::kDriver | status::kFeaturesOk));

    const std::uint8_t s = ReadCommon8(pci_common::kDeviceStatus);
    if ((s & static_cast<std::uint8_t>(status::kFeaturesOk)) == 0U)
    {
        V9P_ERR("PCI feature negotiation failed");
        WriteCommon8(pci_common::kDeviceStatus, static_cast<std::uint8_t>(status::kFailed));
        return -EIO;
    }

    return 0;
}

std::int32_t PciTransportImpl::SetupVirtqueue()
{
    if (common_cfg_ == nullptr)
    {
        return -EINVAL;
    }

    // Select queue 0
    WriteCommon16(pci_common::kQueueSelect, 0U);

    const std::uint16_t max_size = ReadCommon16(pci_common::kQueueSize);
    if (max_size == 0U)
    {
        return -EIO;
    }

    const std::uint16_t queue_size = (max_size < kDefaultQueueSize) ? max_size : kDefaultQueueSize;

    virtqueue_ = std::make_unique<Virtqueue>(queue_size);
    auto rc = virtqueue_->Initialize();
    if (rc != 0)
    {
        return rc;
    }

    WriteCommon16(pci_common::kQueueSize, queue_size);

    // Configure MSI-X vectors in the virtio device.
    // If the PCI-level MSI-X was enabled (via cap module), assign entry 0
    // to queue 0.  Otherwise disable MSI-X so the device uses INTx.
    WriteCommon16(pci_common::kMsixConfig, 0xFFFFU);
    if (use_msix_)
    {
        WriteCommon16(pci_common::kQueueMsixVector, 0U);
        if (ReadCommon16(pci_common::kQueueMsixVector) == 0xFFFFU)
        {
            V9P_WARN(
                "device rejected MSI-X vector, "
                "falling back to INTx");
            use_msix_ = false;
        }
        else
        {
            V9P_DBG("using MSI-X vector 0 for queue 0");
        }
    }
    if (!use_msix_)
    {
        WriteCommon16(pci_common::kQueueMsixVector, 0xFFFFU);
    }

    const std::uint64_t desc_addr = virtqueue_->GetDescAddr();
    const std::uint64_t avail_addr = virtqueue_->GetAvailAddr();
    const std::uint64_t used_addr = virtqueue_->GetUsedAddr();

    V9P_DBG("PCI queue desc=0x%lx avail=0x%lx used=0x%lx size=%u",
            static_cast<unsigned long>(desc_addr),
            static_cast<unsigned long>(avail_addr),
            static_cast<unsigned long>(used_addr),
            queue_size);

    WriteCommon64(pci_common::kQueueDesc, desc_addr);
    WriteCommon64(pci_common::kQueueAvail, avail_addr);
    WriteCommon64(pci_common::kQueueUsed, used_addr);

    // Read queue notify offset for this queue
    queue_notify_off_ = ReadCommon16(pci_common::kQueueNotifyOff);

    V9P_DBG("PCI queue notify_off=%u multiplier=%u", queue_notify_off_, notify_offset_multiplier_);

    // Enable queue
    WriteCommon16(pci_common::kQueueEnable, 1U);

    // Set DRIVER_OK
    WriteCommon8(
        pci_common::kDeviceStatus,
        static_cast<std::uint8_t>(status::kAcknowledge | status::kDriver | status::kFeaturesOk | status::kDriverOk));

    return 0;
}

// --- Device reset (reclaim in-flight descriptors after timeout) ---

std::int32_t PciTransportImpl::ResetDevice()
{
    V9P_WARN("PCI device reset: reclaiming in-flight descriptors");

    // Reset the virtio device — this makes it drop all in-flight buffers.
    if (common_cfg_ != nullptr)
    {
        WriteCommon8(pci_common::kDeviceStatus, 0U);

        // Virtio spec §2.1.1: wait for device_status to read back 0.
        for (int i = 0; i < 100; ++i)
        {
            if (ReadCommon8(pci_common::kDeviceStatus) == 0U)
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
    return InitializeDevice();
}

// --- Data exchange ---

std::int32_t PciTransportImpl::Exchange(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response)
{
    if ((virtqueue_ == nullptr) || (dma_req_virt_ == nullptr) || (dma_resp_virt_ == nullptr))
    {
        return -EINVAL;
    }

    // Zero the response buffer so stale data cannot leak across requests.
    std::memset(dma_resp_virt_, 0, kMaxMessageSize);

    std::memcpy(dma_req_virt_, request.data(), request.size());

    auto head =
        virtqueue_->AddBuf(dma_req_phys_, static_cast<std::uint32_t>(request.size()), dma_resp_phys_, kMaxMessageSize);
    if (head < 0)
    {
        V9P_ERR("PCI AddBuf failed: %d", head);
        return head;
    }

    virtqueue_->Kick();

    // Notify device via the queue-specific notify offset
    if (notify_base_ != nullptr)
    {
        volatile std::uint8_t* notify_addr =
            notify_base_ + (static_cast<std::uint32_t>(queue_notify_off_) * notify_offset_multiplier_);
        *reinterpret_cast<volatile std::uint16_t*>(notify_addr) = 0U;
    }

    std::uint32_t out_len = 0;
    auto rc = WaitForCompletion(out_len);
    if (rc < 0)
    {
        // Log which 9P message type timed out (type byte is at offset 4).
        if (request.size() > 4U)
        {
            V9P_ERR("PCI Exchange failed (rc=%d) for 9P msg type %u", rc, static_cast<unsigned>(request[4]));
        }
        // Device may still hold our descriptors/buffers.  A full reset is the
        // only spec-compliant way to reclaim them before the next Exchange().
        (void)ResetDevice();
        return rc;
    }

    response.resize(out_len);
    std::memcpy(response.data(), dma_resp_virt_, out_len);
    return 0;
}

std::int32_t PciTransportImpl::WaitForCompletion(std::uint32_t& out_len)
{
    // Fast path: with KVM the device often completes before we get here.
    auto rc = virtqueue_->GetBuf(out_len);
    if (rc >= 0)
    {
        // Drain any pending pulse so it doesn't accumulate.
        if (isr_cfg_ != nullptr && !use_msix_)
        {
            (void)*reinterpret_cast<volatile const std::uint8_t*>(isr_cfg_);
        }
        struct _pulse pulse;
        std::uint64_t no_wait = 0;
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, nullptr, &no_wait, nullptr);
        if (MsgReceivePulse(chid_, &pulse, sizeof(pulse), nullptr) != -1)
        {
            InterruptUnmask(static_cast<int>(irq_num_), irq_id_);
        }
        return 0;
    }

    // Slow path: block until the device raises an interrupt.
    constexpr std::uint64_t kTimeoutNs = 30ULL * 1000ULL * 1000ULL * 1000ULL;
    struct _pulse pulse;

    for (;;)
    {
        std::uint64_t timeout_nsec = kTimeoutNs;
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, nullptr, &timeout_nsec, nullptr);

        int rcvid = MsgReceivePulse(chid_, &pulse, sizeof(pulse), nullptr);

        // For INTx, read the ISR status register to de-assert the line.
        if (isr_cfg_ != nullptr && !use_msix_)
        {
            (void)*reinterpret_cast<volatile const std::uint8_t*>(isr_cfg_);
        }

        if (rcvid == -1)
        {
            // Timeout — last-resort check.
            rc = virtqueue_->GetBuf(out_len);
            if (rc >= 0)
            {
                return 0;
            }
            V9P_ERR("PCI WaitForCompletion timeout: %s", strerror(errno));
            return -errno;
        }

        InterruptUnmask(static_cast<int>(irq_num_), irq_id_);

        rc = virtqueue_->GetBuf(out_len);
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

        // The interrupt fired but the used ring is still empty.  This can
        // happen with INTx (shared IRQ, spurious wakeup) or with MSI-X under
        // TCG emulation (ioeventfd race: the interrupt is delivered before
        // the used-ring write is visible to the guest CPU).  In both cases
        // the correct action is to loop back and wait for the next interrupt
        // or the 30-second timeout.
        V9P_WARN("PCI WaitForCompletion: used ring empty after %s interrupt, retrying", use_msix_ ? "MSI-X" : "INTx");
    }
    return 0;
}

std::int32_t PciTransportImpl::GetMountTag(std::string& tag)
{
    if (device_cfg_ == nullptr)
    {
        return -EINVAL;
    }

    const auto tag_len = *reinterpret_cast<volatile const std::uint16_t*>(device_cfg_);
    if (tag_len == 0U || tag_len > 256U)
    {
        return -EINVAL;
    }

    tag.resize(tag_len);
    for (std::uint16_t i = 0U; i < tag_len; ++i)
    {
        tag[i] = static_cast<char>(*(device_cfg_ + 2U + i));
    }

    return 0;
}

void PciTransportImpl::Shutdown()
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

    // Reset device before unmapping
    if (common_cfg_ != nullptr)
    {
        WriteCommon8(pci_common::kDeviceStatus, 0U);
    }

    // Disable MSI-X in PCI config space before unmapping BARs
    if (use_msix_ && pci_handle_ != nullptr && msix_cap_offset_ != 0U)
    {
        const auto hdl = reinterpret_cast<pci_devhdl_t>(pci_handle_);
        uint16_t val = 0U;
        pci_device_cfg_wr16(hdl, msix_cap_offset_ + 2U, 0U, &val);
        use_msix_ = false;
    }

    // Release allocated MSI-X IRQ
    if (irq_num_ != 0U && msix_cap_offset_ != 0U)
    {
        rsrc_request_t irq_rel{};
        irq_rel.length = 1U;
        irq_rel.start = irq_num_;
        irq_rel.end = irq_num_;
        irq_rel.flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RANGE;
        rsrcdbmgr_detach(&irq_rel, 1);
    }
    msix_table_ = nullptr;

    for (int i = 0; i < 6; ++i)
    {
        if (bar_virt_[i] != nullptr)
        {
            munmap_device_memory(const_cast<std::uint8_t*>(const_cast<volatile std::uint8_t*>(bar_virt_[i])),
                                 static_cast<std::size_t>(bar_size_[i]));
            bar_virt_[i] = nullptr;
        }
    }
    common_cfg_ = nullptr;
    notify_base_ = nullptr;
    device_cfg_ = nullptr;
    isr_cfg_ = nullptr;

    if (pci_handle_ != nullptr)
    {
        pci_device_detach(reinterpret_cast<pci_devhdl_t>(pci_handle_));
        pci_handle_ = nullptr;
    }
    virtqueue_.reset();
}

}  // namespace virtio9p
