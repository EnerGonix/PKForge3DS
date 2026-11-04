// Stand-in for Checkpoint's Logging, which spi.cpp calls: PKForge keeps no log, so the calls do nothing.
#pragma once

namespace Logging
{
    template <typename... Args>
    void info(Args&&...)
    {
    }
    template <typename... Args>
    void warning(Args&&...)
    {
    }
    template <typename... Args>
    void error(Args&&...)
    {
    }
}
