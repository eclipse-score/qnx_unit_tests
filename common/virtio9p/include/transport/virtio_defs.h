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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTIO_DEFS_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTIO_DEFS_H

#include <cstdint>

namespace virtio9p
{

/// Virtio MMIO register offsets (virtio 1.0+ spec).
namespace mmio
{
constexpr std::uint32_t kMagicValue = 0x000;
constexpr std::uint32_t kVersion = 0x004;
constexpr std::uint32_t kDeviceId = 0x008;
constexpr std::uint32_t kVendorId = 0x00C;
constexpr std::uint32_t kDeviceFeatures = 0x010;
constexpr std::uint32_t kDeviceFeaturesSel = 0x014;
constexpr std::uint32_t kDriverFeatures = 0x020;
constexpr std::uint32_t kDriverFeaturesSel = 0x024;
// Legacy (v1) only registers
constexpr std::uint32_t kGuestPageSize = 0x028;
constexpr std::uint32_t kQueueSel = 0x030;
constexpr std::uint32_t kQueueNumMax = 0x034;
constexpr std::uint32_t kQueueNum = 0x038;
// Legacy (v1) only registers
constexpr std::uint32_t kQueueAlign = 0x03C;
constexpr std::uint32_t kQueuePfn = 0x040;
// Modern (v2) only register
constexpr std::uint32_t kQueueReady = 0x044;
constexpr std::uint32_t kQueueNotify = 0x050;
constexpr std::uint32_t kInterruptStatus = 0x060;
constexpr std::uint32_t kInterruptAck = 0x064;
constexpr std::uint32_t kStatus = 0x070;
// Modern (v2) only registers
constexpr std::uint32_t kQueueDescLow = 0x080;
constexpr std::uint32_t kQueueDescHigh = 0x084;
constexpr std::uint32_t kQueueAvailLow = 0x090;
constexpr std::uint32_t kQueueAvailHigh = 0x094;
constexpr std::uint32_t kQueueUsedLow = 0x0A0;
constexpr std::uint32_t kQueueUsedHigh = 0x0A4;
constexpr std::uint32_t kConfigGeneration = 0x0FC;
constexpr std::uint32_t kConfig = 0x100;
}  // namespace mmio

/// Virtio device status bits.
namespace status
{
constexpr std::uint32_t kAcknowledge = 1U;
constexpr std::uint32_t kDriver = 2U;
constexpr std::uint32_t kDriverOk = 4U;
constexpr std::uint32_t kFeaturesOk = 8U;
constexpr std::uint32_t kDeviceNeedsReset = 64U;
constexpr std::uint32_t kFailed = 128U;
}  // namespace status

/// Virtio device IDs.
constexpr std::uint32_t kVirtio9pDeviceId = 9U;

/// PCI vendor/device IDs for virtio-9p.
constexpr std::uint16_t kVirtioPciVendorId = 0x1AF4U;
constexpr std::uint16_t kVirtio9pPciDeviceId = 0x1009U;

/// Virtio PCI capability structure types (virtio 1.0 spec, section 4.1.4).
namespace pci_cap
{
constexpr std::uint8_t kCommonCfg = 1U;  ///< VIRTIO_PCI_CAP_COMMON_CFG
constexpr std::uint8_t kNotifyCfg = 2U;  ///< VIRTIO_PCI_CAP_NOTIFY_CFG
constexpr std::uint8_t kIsrCfg = 3U;     ///< VIRTIO_PCI_CAP_ISR_CFG
constexpr std::uint8_t kDeviceCfg = 4U;  ///< VIRTIO_PCI_CAP_DEVICE_CFG
constexpr std::uint8_t kPciCfg = 5U;     ///< VIRTIO_PCI_CAP_PCI_CFG
}  // namespace pci_cap

/// Offsets within the virtio PCI common configuration structure.
namespace pci_common
{
constexpr std::uint32_t kDeviceFeatureSelect = 0x00U;
constexpr std::uint32_t kDeviceFeature = 0x04U;
constexpr std::uint32_t kDriverFeatureSelect = 0x08U;
constexpr std::uint32_t kDriverFeature = 0x0CU;
constexpr std::uint32_t kMsixConfig = 0x10U;
constexpr std::uint32_t kNumQueues = 0x12U;
constexpr std::uint32_t kDeviceStatus = 0x14U;
constexpr std::uint32_t kConfigGeneration = 0x15U;
constexpr std::uint32_t kQueueSelect = 0x16U;
constexpr std::uint32_t kQueueSize = 0x18U;
constexpr std::uint32_t kQueueMsixVector = 0x1AU;
constexpr std::uint32_t kQueueEnable = 0x1CU;
constexpr std::uint32_t kQueueNotifyOff = 0x1EU;
constexpr std::uint32_t kQueueDesc = 0x20U;
constexpr std::uint32_t kQueueAvail = 0x28U;
constexpr std::uint32_t kQueueUsed = 0x30U;
}  // namespace pci_common

/// Virtqueue descriptor flags.
constexpr std::uint16_t kVringDescFlagNext = 1U;
constexpr std::uint16_t kVringDescFlagWrite = 2U;

/// Virtqueue sizes.
constexpr std::uint16_t kDefaultQueueSize = 128U;

/// Virtqueue descriptor.
struct VringDesc
{
    std::uint64_t addr;
    std::uint32_t len;
    std::uint16_t flags;
    std::uint16_t next;
};

/// Virtqueue available ring entry.
struct VringAvail
{
    std::uint16_t flags;
    std::uint16_t idx;
    // ring[] follows (variable length)
};

/// Virtqueue used ring element.
struct VringUsedElem
{
    std::uint32_t id;
    std::uint32_t len;
};

/// Virtqueue used ring.
struct VringUsed
{
    std::uint16_t flags;
    std::uint16_t idx;
    // ring[] follows (variable length)
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_VIRTIO_DEFS_H
