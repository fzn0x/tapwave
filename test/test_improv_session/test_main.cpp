#include "doctest_main.hpp"

#include "core/improv_session.hpp"

using namespace tapwave;
using namespace tapwave::improv;

namespace {

struct Sent {
    std::vector<State> states;
    std::vector<Error> errors;
    std::vector<std::pair<std::uint8_t, std::vector<std::string>>> results;
};

struct Device {
    std::uint32_t now = 1000;
    ConnectResult connect_result = ConnectResult::Pending;
    std::string connected_ssid;
    std::string connected_password;
    bool online = false;
    int identified = 0;
    bool scanning = false;
    std::vector<Network> networks{{"Home", -52, "WPA2"}, {"Cafe", -80, "NO"}};
    std::string hostname = "tapwave-a1b2c3";
    std::string name = "Tapwave";
};

struct Rig {
    Device device;
    Sent sent;
    Session session;

    explicit Rig(Transport transport) : session(transport, hooks(), output()) {}

    DeviceHooks hooks() {
        DeviceHooks h;
        h.now_ms = [this] { return device.now; };
        h.connect = [this](const std::string& ssid, const std::string& password) {
            device.connected_ssid = ssid;
            device.connected_password = password;
        };
        h.connect_result = [this] { return device.connect_result; };
        h.online = [this] { return device.online; };
        h.device_url = [this] { return device.online ? std::string("http://192.168.1.50/") : ""; };
        h.identify = [this] { ++device.identified; };
        h.device_info = [] {
            return std::vector<std::string>{"Tapwave", "0.1.0", "ESP32", "Tapwave"};
        };
        h.start_scan = [this] { device.scanning = true; };
        h.scan_results = [this]() -> std::optional<std::vector<Network>> {
            if (device.scanning) {
                return std::nullopt;
            }
            return device.networks;
        };
        h.hostname = [this] { return device.hostname; };
        h.set_hostname = [this](const std::string& name) {
            if (name.find('_') != std::string::npos) {
                return false;
            }
            device.hostname = name;
            return true;
        };
        h.device_name = [this] { return device.name; };
        h.set_device_name = [this](const std::string& name) {
            device.name = name;
            return true;
        };
        return h;
    }

    Output output() {
        Output o;
        o.state = [this](State s) { sent.states.push_back(s); };
        o.error = [this](Error e) { sent.errors.push_back(e); };
        o.result = [this](std::uint8_t command, const std::vector<std::string>& strings) {
            sent.results.emplace_back(command, strings);
        };
        return o;
    }
};

Rpc rpc(Command command, std::vector<std::string> args = {}) {
    return Rpc{static_cast<std::uint8_t>(command), std::move(args)};
}

}  // namespace

TEST_CASE("credentials start provisioning and success sends the device URL") {
    Rig rig(Transport::Serial);
    rig.session.begin(false);
    CHECK(rig.session.state() == State::Authorized);

    rig.session.handle(rpc(Command::WifiSettings, {"Home", "secret123"}));
    CHECK(rig.sent.states == std::vector<State>{State::Provisioning});
    CHECK(rig.device.connected_ssid == "Home");
    CHECK(rig.device.connected_password == "secret123");

    rig.session.loop();  // still pending
    CHECK(rig.sent.results.empty());

    rig.device.connect_result = ConnectResult::Succeeded;
    rig.device.online = true;
    rig.session.loop();
    CHECK(rig.session.state() == State::Provisioned);
    CHECK(rig.sent.states.back() == State::Provisioned);
    REQUIRE(rig.sent.results.size() == 1);
    CHECK(rig.sent.results[0].first == 0x01);
    CHECK(rig.sent.results[0].second == std::vector<std::string>{"http://192.168.1.50/"});
}

TEST_CASE("a failed connection reports the error and accepts new credentials") {
    Rig rig(Transport::Ble);
    rig.session.begin(false);
    rig.session.handle(rpc(Command::WifiSettings, {"Home", "wrong"}));
    rig.device.connect_result = ConnectResult::Failed;
    rig.session.loop();
    CHECK(rig.sent.errors == std::vector<Error>{Error::UnableToConnect});
    CHECK(rig.session.state() == State::Authorized);

    // The next command clears the error first.
    rig.device.connect_result = ConnectResult::Pending;
    rig.session.handle(rpc(Command::WifiSettings, {"Home", "right"}));
    CHECK(rig.sent.errors == std::vector<Error>{Error::UnableToConnect, Error::None});
}

TEST_CASE("a connection that never resolves times out") {
    Rig rig(Transport::Serial);
    rig.session.begin(false);
    rig.session.handle(rpc(Command::WifiSettings, {"Home", "x"}));
    rig.device.now += Session::kConnectTimeoutMs - 1;
    rig.session.loop();
    CHECK(rig.sent.errors.empty());
    rig.device.now += 2;
    rig.session.loop();
    CHECK(rig.sent.errors == std::vector<Error>{Error::UnableToConnect});
}

TEST_CASE("serial command 0x02 reports the state, and the URL once provisioned") {
    Rig rig(Transport::Serial);
    rig.session.begin(true);
    rig.device.online = true;
    rig.session.handle(rpc(Command::CurrentStateOrIdentify));
    CHECK(rig.sent.states == std::vector<State>{State::Provisioned});
    REQUIRE(rig.sent.results.size() == 1);
    CHECK(rig.sent.results[0].second.front() == "http://192.168.1.50/");
    CHECK(rig.device.identified == 0);
}

TEST_CASE("BLE command 0x02 is identify") {
    Rig rig(Transport::Ble);
    rig.session.begin(false);
    rig.session.handle(rpc(Command::CurrentStateOrIdentify));
    CHECK(rig.device.identified == 1);
    CHECK(rig.sent.states.empty());
}

TEST_CASE("device info") {
    Rig rig(Transport::Serial);
    rig.session.handle(rpc(Command::DeviceInfo));
    REQUIRE(rig.sent.results.size() == 1);
    CHECK(rig.sent.results[0].second ==
          std::vector<std::string>{"Tapwave", "0.1.0", "ESP32", "Tapwave"});
}

TEST_CASE("a Wi-Fi scan answers each network, then an empty result") {
    Rig serial(Transport::Serial);
    serial.session.handle(rpc(Command::WifiNetworks));
    CHECK(serial.device.scanning);
    serial.session.loop();
    CHECK(serial.sent.results.empty());
    serial.device.scanning = false;
    serial.session.loop();
    REQUIRE(serial.sent.results.size() == 3);
    CHECK(serial.sent.results[0].second == std::vector<std::string>{"Home", "-52", "YES"});
    CHECK(serial.sent.results[1].second == std::vector<std::string>{"Cafe", "-80", "NO"});
    CHECK(serial.sent.results[2].second.empty());

    Rig ble(Transport::Ble);
    ble.session.handle(rpc(Command::WifiNetworks));
    ble.device.scanning = false;
    ble.session.loop();
    REQUIRE(ble.sent.results.size() == 3);
    CHECK(ble.sent.results[0].second == std::vector<std::string>{"Home", "-52", "WPA2"});
}

TEST_CASE("hostname and device name, read and written") {
    Rig rig(Transport::Serial);
    rig.session.handle(rpc(Command::Hostname));
    CHECK(rig.sent.results.back().second == std::vector<std::string>{"tapwave-a1b2c3"});
    rig.session.handle(rpc(Command::Hostname, {"kids-box"}));
    CHECK(rig.sent.results.back().second == std::vector<std::string>{"kids-box"});
    rig.session.handle(rpc(Command::Hostname, {"bad_name"}));
    CHECK(rig.sent.errors.back() == Error::BadHostname);
    rig.session.handle(rpc(Command::DeviceName, {"Kids box"}));
    CHECK(rig.device.name == "Kids box");
    CHECK(rig.sent.results.back().second == std::vector<std::string>{"Kids box"});
}

TEST_CASE("network state flags") {
    Rig rig(Transport::Serial);
    rig.session.handle(rpc(Command::NetworkState));
    CHECK(rig.sent.results.back().second == std::vector<std::string>{"2"});
    rig.device.online = true;
    rig.session.handle(rpc(Command::NetworkState));
    CHECK(rig.sent.results.back().second == std::vector<std::string>{"3", "http://192.168.1.50/"});
}

TEST_CASE("packets the transport could not parse are reported") {
    Rig rig(Transport::Ble);
    rig.session.begin(false);
    rig.session.report(Error::InvalidRpc);
    CHECK(rig.sent.errors == std::vector<Error>{Error::InvalidRpc});
}

TEST_CASE("unknown commands report UnknownRpc") {
    Rig rig(Transport::Serial);
    rig.session.handle(Rpc{0x42, {}});
    CHECK(rig.sent.errors == std::vector<Error>{Error::UnknownRpc});
}
