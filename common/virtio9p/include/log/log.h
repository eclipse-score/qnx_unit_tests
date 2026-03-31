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
#ifndef PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_LOG_LOG_H
#define PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_LOG_LOG_H

/// @file Simple logging for fs-virtio9p.
///
/// All messages go to slog2. Error-severity messages are additionally
/// written to stderr so they appear on the serial console during boot.

#include <sys/slog2.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace virtio9p
{
namespace log
{

/// Call once at startup (before any logging) to register the slog2 buffer.
void Initialize();

/// Log at the given slog2 severity. Writes to slog2 and (for errors)
/// also to stderr.
void Log(std::uint8_t severity, const char* format, ...) __attribute__((format(printf, 2, 3)));

}  // namespace log
}  // namespace virtio9p

// Convenience macros — severity mirrors slog2 levels.
#define V9P_ERR(...) virtio9p::log::Log(SLOG2_ERROR, "fs-virtio9p: " __VA_ARGS__)
#define V9P_WARN(...) virtio9p::log::Log(SLOG2_WARNING, "fs-virtio9p: " __VA_ARGS__)
#define V9P_INFO(...) virtio9p::log::Log(SLOG2_INFO, "fs-virtio9p: " __VA_ARGS__)
#define V9P_DBG(...) virtio9p::log::Log(SLOG2_DEBUG1, "fs-virtio9p: " __VA_ARGS__)

#endif  // PLATFORM_AAS_TOOLS_QNX_UNIT_TESTS_COMMON_VIRTIO9P_LOG_LOG_H
