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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_TRANSPORT_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_TRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace virtio9p
{

/// Abstract transport interface for sending/receiving 9P messages over virtio.
///
/// Concrete implementations handle virtqueue setup and message exchange for
/// a specific bus type (MMIO or PCI).
class Transport
{
  public:
    Transport() = default;
    virtual ~Transport() = default;

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    /// Initialize the transport (map device memory, set up virtqueues).
    /// @return 0 on success, negative errno on failure.
    virtual std::int32_t Initialize() = 0;

    /// Send a request and receive the response synchronously.
    /// @param request The serialized 9P request bytes.
    /// @param response Buffer to receive the serialized 9P response.
    /// @return 0 on success, negative errno on failure.
    virtual std::int32_t Exchange(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response) = 0;

    /// Read the mount tag from the virtio config space.
    /// @param tag Output string for the mount tag.
    /// @return 0 on success, negative errno on failure.
    virtual std::int32_t GetMountTag(std::string& tag) = 0;

    /// Reset the virtio device and re-initialize the virtqueue and DMA buffers.
    /// Must be called after a timeout to reclaim in-flight descriptors.
    /// @return 0 on success, negative errno on failure.
    virtual std::int32_t ResetDevice() = 0;

    /// Shut down the transport and release resources.
    virtual void Shutdown() = 0;
};

}  // namespace virtio9p

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_TRANSPORT_TRANSPORT_H
