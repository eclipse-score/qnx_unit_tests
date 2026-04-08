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
#include "log/log.h"

#include <sys/slog2.h>
#include <cstdarg>
#include <cstdio>

namespace virtio9p
{
namespace log
{

namespace
{

slog2_buffer_t g_slog_buffer{};
bool g_initialized{false};

}  // namespace

void Initialize()
{
    slog2_buffer_set_config_t config{};
    config.buffer_set_name = "fs-virtio9p";
    config.num_buffers = 1;
    config.verbosity_level = SLOG2_DEBUG1;
    config.buffer_config[0].buffer_name = "default";
    config.buffer_config[0].num_pages = 8;  // 8 × 4 KiB = 32 KiB

    if (slog2_register(&config, &g_slog_buffer, 0) == 0)
    {
        g_initialized = true;
    }
    else
    {
        fprintf(stderr, "fs-virtio9p: slog2_register failed, falling back to stderr only\n");
    }
}

void Log(std::uint8_t severity, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    if (g_initialized)
    {
        va_list args_copy;
        va_copy(args_copy, args);
        vslog2f(g_slog_buffer, 0, severity, format, args_copy);
        va_end(args_copy);
    }

    // Errors additionally go to stderr (serial console) for boot-time visibility
    if (severity <= SLOG2_ERROR)
    {
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }

    va_end(args);
}

}  // namespace log
}  // namespace virtio9p
