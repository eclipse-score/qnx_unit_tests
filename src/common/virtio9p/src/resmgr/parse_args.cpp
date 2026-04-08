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
#include "resmgr/fs_virtio9p.h"

#include <cstdlib>
#include <string>

namespace virtio9p
{

namespace
{

/// Parse a single key=value option and apply it to config.
void ApplyOption(const std::string& option, FsConfig& config)
{
    const auto eq_pos = option.find('=');
    if (eq_pos == std::string::npos)
    {
        return;
    }
    const auto key = option.substr(0, eq_pos);
    const auto value = option.substr(eq_pos + 1);

    if (key == "smem")
    {
        config.mmio_base = std::strtoull(value.c_str(), nullptr, 0);
        if (config.transport_type.empty())
        {
            config.transport_type = "mmio";
        }
    }
    else if (key == "irq")
    {
        config.irq = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
    }
    else if (key == "transport")
    {
        config.transport_type = value;
    }
}

}  // namespace

std::int32_t ParseArgs(int argc, char* argv[], FsConfig& config)
{
    config.mount_point.clear();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);

        if (arg == "-d")
        {
            config.daemonize = true;
        }
        else if (arg == "-o" && (i + 1) < argc)
        {
            // Parse comma-separated key=value options
            std::string opts(argv[++i]);
            std::string::size_type pos = 0;
            while (pos < opts.size())
            {
                auto comma = opts.find(',', pos);
                if (comma == std::string::npos)
                {
                    comma = opts.size();
                }
                ApplyOption(opts.substr(pos, comma - pos), config);
                pos = comma + 1;
            }
        }
        else if (arg[0] != '-')
        {
            // Positional argument: mountpoint (last one wins)
            config.mount_point = arg;
        }
    }

    if (config.mount_point.empty())
    {
        config.mount_point = "/mnt/host";
    }

    // Default to PCI if no transport specified
    if (config.transport_type.empty())
    {
        config.transport_type = "pci";
    }

    return 0;
}

}  // namespace virtio9p
