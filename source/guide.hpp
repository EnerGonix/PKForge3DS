// The quick guide: a few pages on the basics, shown once on the first start (after the title screen) and
// again from Settings > Quick guide. sdmc:/3ds/PKForge/guide-seen.txt marks it as seen.
#pragma once

#include "app.hpp"
#include <memory>

namespace guide
{
    bool seen();
    std::unique_ptr<Screen> open();
}
