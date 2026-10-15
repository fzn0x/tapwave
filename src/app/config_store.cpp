#include "app/config_store.hpp"

#include <Preferences.h>
#include <esp_log.h>

#include <string>

namespace tapwave::app {

namespace {

constexpr const char* kTag = "config";
constexpr const char* kNamespace = "tapwave";
constexpr const char* kKey = "config";
constexpr const char* kBackupKey = "config.bak";

}  // namespace

config::LoadResult ConfigStore::load(Chip chip) {
    Preferences prefs;
    std::string stored;
    if (prefs.begin(kNamespace, /*readOnly=*/true)) {
        const std::size_t length = prefs.getBytesLength(kKey);
        if (length > 0) {
            stored.resize(length);
            prefs.getBytes(kKey, stored.data(), length);
        }
        prefs.end();
    }

    config::LoadResult result = config::load(stored, chip);
    for (const auto& issue : result.issues) {
        ESP_LOGW(kTag, "%s%s%s", issue.path.c_str(), issue.path.empty() ? "" : ": ",
                 issue.message.c_str());
    }
    if (result.reset && !stored.empty() && prefs.begin(kNamespace, false)) {
        prefs.putBytes(kBackupKey, stored.data(), stored.size());
        prefs.end();
        ESP_LOGW(kTag, "kept the unusable settings as %s", kBackupKey);
    }
    if (result.migrated) {
        save(result.config);
        ESP_LOGI(kTag, "settings upgraded to schema %d", config::kSchemaVersion);
    }
    return result;
}

bool ConfigStore::save(const config::Config& config) {
    const std::string document = config::save(config);
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    const bool ok = prefs.putBytes(kKey, document.data(), document.size()) == document.size();
    prefs.end();
    if (!ok) {
        ESP_LOGE(kTag, "could not save the settings");
    }
    return ok;
}

void ConfigStore::erase() {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
}

}  // namespace tapwave::app
