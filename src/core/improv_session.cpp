#include "core/improv_session.hpp"

namespace tapwave::improv {

Session::Session(Transport transport, DeviceHooks hooks, Output output)
    : transport_(transport), hooks_(std::move(hooks)), out_(std::move(output)) {}

void Session::begin(bool provisioned) {
    state_ = provisioned ? State::Provisioned : State::Authorized;
}

void Session::set_state(State state) {
    state_ = state;
    out_.state(state_);
}

void Session::set_error(Error error) {
    error_ = error;
    out_.error(error_);
}

void Session::report(Error error) {
    set_error(error);
}

void Session::send_provisioned_result() {
    const std::string url = hooks_.device_url();
    out_.result(static_cast<std::uint8_t>(Command::WifiSettings),
                url.empty() ? std::vector<std::string>{} : std::vector<std::string>{url});
}

void Session::handle(const Rpc& rpc) {
    if (error_ != Error::None) {
        set_error(Error::None);  // every command starts with a clean error state
    }
    const auto command = static_cast<Command>(rpc.command);
    switch (command) {
        case Command::WifiSettings:
            set_state(State::Provisioning);
            hooks_.connect(rpc.args[0], rpc.args[1]);
            connecting_ = true;
            connect_deadline_ = hooks_.now_ms() + kConnectTimeoutMs;
            return;

        case Command::CurrentStateOrIdentify:
            if (transport_ == Transport::Ble) {
                hooks_.identify();
                return;
            }
            out_.state(state_);
            if (state_ == State::Provisioned) {
                send_provisioned_result();
            }
            return;

        case Command::DeviceInfo:
            out_.result(rpc.command, hooks_.device_info());
            return;

        case Command::WifiNetworks:
            hooks_.start_scan();
            scanning_ = true;
            return;

        case Command::Hostname:
            if (!rpc.args.empty()) {
                if (!hooks_.set_hostname(rpc.args[0])) {
                    set_error(Error::BadHostname);
                    return;
                }
            }
            out_.result(rpc.command, {hooks_.hostname()});
            return;

        case Command::DeviceName:
            if (!rpc.args.empty()) {
                if (!hooks_.set_device_name(rpc.args[0])) {
                    set_error(Error::Unknown);
                    return;
                }
            }
            out_.result(rpc.command, {hooks_.device_name()});
            return;

        case Command::NetworkState: {
            const bool online = hooks_.online();
            const std::uint8_t flags = kNetworkWifi | (online ? kNetworkOnline : 0);
            std::vector<std::string> strings{std::to_string(flags)};
            if (online) {
                strings.push_back(hooks_.device_url());
            }
            out_.result(rpc.command, strings);
            return;
        }
    }
    set_error(Error::UnknownRpc);
}

void Session::loop() {
    if (connecting_) {
        const ConnectResult result = hooks_.connect_result();
        const bool timed_out = static_cast<std::int32_t>(hooks_.now_ms() - connect_deadline_) > 0;
        if (result == ConnectResult::Succeeded) {
            connecting_ = false;
            set_state(State::Provisioned);
            send_provisioned_result();
        } else if (result == ConnectResult::Failed || timed_out) {
            connecting_ = false;
            set_error(Error::UnableToConnect);
            set_state(State::Authorized);
        }
    }
    if (scanning_) {
        if (const auto networks = hooks_.scan_results()) {
            scanning_ = false;
            const auto command = static_cast<std::uint8_t>(Command::WifiNetworks);
            for (const Network& n : *networks) {
                const std::string auth =
                    transport_ == Transport::Serial ? (n.auth == "NO" ? "NO" : "YES") : n.auth;
                out_.result(command, {n.ssid, std::to_string(n.rssi), auth});
            }
            out_.result(command, {});
        }
    }
}

}  // namespace tapwave::improv
