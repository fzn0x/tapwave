// Keeps the configuration in NVS (ARCHITECTURE.md section 10).
#pragma once

#include "core/config.hpp"

namespace tapwave::app {

class ConfigStore {
public:
    // Loads and migrates. An unusable document is copied to "config.bak" before defaults are
    // used, and a migrated one is saved back right away.
    config::LoadResult load(Chip chip);

    bool save(const config::Config& config);

    // Factory reset: removes the configuration and its backup.
    void erase();
};

}  // namespace tapwave::app
