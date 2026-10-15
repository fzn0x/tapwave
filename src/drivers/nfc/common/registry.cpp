#include "drivers/nfc/common/registry.hpp"

#include <algorithm>

namespace tapwave::nfc {

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

void Registry::add(const DriverEntry& entry) {
    entries_.push_back(entry);
}

std::vector<DriverEntry> Registry::entries() const {
    std::vector<DriverEntry> sorted = entries_;
    std::ranges::stable_sort(sorted, {}, &DriverEntry::priority);
    return sorted;
}

Detection detect(const Registry& registry, const ProbeContext& context, std::string_view driver) {
    Detection detection;
    const bool any = driver == "auto";
    bool known = any;
    for (const DriverEntry& entry : registry.entries()) {
        if (!any && entry.id != driver) {
            continue;
        }
        known = true;
        auto result = entry.probe(context);
        if (result && *result) {
            detection.attempts.push_back(
                {std::string(entry.id), "found " + (*result)->info().summary()});
            detection.reader = std::move(*result);
            return detection;
        }
        detection.attempts.push_back(
            {std::string(entry.id), result ? std::string("no reader answered") : result.error()});
    }
    if (!known) {
        detection.attempts.push_back(
            {std::string(driver), "this firmware has no driver with that name"});
    }
    return detection;
}

}  // namespace tapwave::nfc
