#include "doctest_main.hpp"

#include <algorithm>

#include "drivers/nfc/common/registry.hpp"

using namespace tapwave;
using namespace tapwave::nfc;

namespace {

class StubReader final : public TagReader {
public:
    explicit StubReader(std::string model) { info_.model = std::move(model); }
    const ReaderInfo& info() const override { return info_; }
    std::expected<std::optional<Uid>, Error> detect() override { return std::optional<Uid>{}; }
    std::expected<TagRead, Error> read() override { return std::unexpected(Error::NoTag); }
    std::expected<void, Error> write_ndef(ByteView) override {
        return std::unexpected(Error::NotSupported);
    }

private:
    ReaderInfo info_;
};

int probes = 0;

ProbeResult absent(const ProbeContext&) {
    ++probes;
    return std::unexpected(std::string("no answer on I2C address 0x24"));
}

ProbeResult silent(const ProbeContext&) {
    ++probes;
    return std::unique_ptr<TagReader>{};
}

ProbeResult present(const ProbeContext& context) {
    ++probes;
    if (context.pins.nfc_cs < 0) {
        return std::unexpected(std::string("nfc_cs pin not set"));
    }
    return std::make_unique<StubReader>("RC522");
}

ProbeResult also_present(const ProbeContext&) {
    ++probes;
    return std::make_unique<StubReader>("PN532");
}

Registry registry_with_drivers() {
    Registry registry;
    registry.add({"late", 90, "never reached", also_present});
    registry.add({"rc522-spi", 20, "RC522 on SPI", present});
    registry.add({"pn532-i2c", 10, "PN532 on I2C", absent});
    registry.add({"quiet", 15, "returns nothing", silent});
    return registry;
}

}  // namespace

TEST_CASE("entries come back in priority order") {
    const Registry registry = registry_with_drivers();
    const auto entries = registry.entries();
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].id == "pn532-i2c");
    CHECK(entries[1].id == "quiet");
    CHECK(entries[2].id == "rc522-spi");
    CHECK(entries[3].id == "late");
}

TEST_CASE("auto stops at the first reader that answers and reports every attempt") {
    const Registry registry = registry_with_drivers();
    config::Pins pins;
    pins.nfc_cs = 5;
    probes = 0;
    const Detection found = detect(registry, {pins}, "auto");
    REQUIRE(found.reader);
    CHECK(found.reader->info().model == "RC522");
    CHECK(probes == 3);
    REQUIRE(found.attempts.size() == 3);
    CHECK(found.attempts[0] == ProbeAttempt{"pn532-i2c", "no answer on I2C address 0x24"});
    CHECK(found.attempts[1] == ProbeAttempt{"quiet", "no reader answered"});
    CHECK(found.attempts[2].result.starts_with("found RC522"));
}

TEST_CASE("nothing found is not an error") {
    config::Pins pins;  // nfc_cs not set
    Registry only_absent;
    only_absent.add({"pn532-i2c", 10, "", absent});
    const Detection none = detect(only_absent, {pins}, "auto");
    CHECK_FALSE(none.reader);
    CHECK(none.attempts.size() == 1);
}

TEST_CASE("a pinned driver is the only one probed") {
    const Registry registry = registry_with_drivers();
    config::Pins pins;
    probes = 0;
    const Detection pinned = detect(registry, {pins}, "late");
    REQUIRE(pinned.reader);
    CHECK(pinned.reader->info().model == "PN532");
    CHECK(probes == 1);

    const Detection missing_pin = detect(registry, {pins}, "rc522-spi");
    CHECK_FALSE(missing_pin.reader);
    CHECK(missing_pin.attempts.front().result == "nfc_cs pin not set");
}

TEST_CASE("an unknown driver name says so") {
    const Registry registry = registry_with_drivers();
    config::Pins pins;
    const Detection unknown = detect(registry, {pins}, "pn5180-spi");
    CHECK_FALSE(unknown.reader);
    REQUIRE(unknown.attempts.size() == 1);
    CHECK(unknown.attempts[0].driver == "pn5180-spi");
}

namespace {
ProbeResult registered_probe(const ProbeContext&) {
    return std::make_unique<StubReader>("Macro");
}
TAPWAVE_REGISTER_NFC_DRIVER("macro-test", 99, "registered by the macro", registered_probe);
}  // namespace

TEST_CASE("the registration macro adds to the global registry") {
    const auto entries = Registry::instance().entries();
    const auto it = std::ranges::find(entries, std::string_view("macro-test"), &DriverEntry::id);
    REQUIRE(it != entries.end());
    CHECK(it->priority == 99);
}
