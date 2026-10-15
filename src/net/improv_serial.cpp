#include "net/improv_serial.hpp"

namespace tapwave::net {

void ImprovSerial::begin(Stream& serial, improv::DeviceHooks hooks, bool provisioned) {
    serial_ = &serial;
    improv::Output output;
    output.state = [this](improv::State state) {
        send(improv::PacketType::CurrentState, Bytes{static_cast<std::uint8_t>(state)});
    };
    output.error = [this](improv::Error error) {
        send(improv::PacketType::ErrorState, Bytes{static_cast<std::uint8_t>(error)});
    };
    output.result = [this](std::uint8_t command, const std::vector<std::string>& strings) {
        send(improv::PacketType::RpcResult, improv::build_rpc_result(command, strings, false));
    };
    session_ = std::make_unique<improv::Session>(improv::Transport::Serial, std::move(hooks),
                                                 std::move(output));
    session_->begin(provisioned);
}

void ImprovSerial::send(improv::PacketType type, ByteView data) {
    const Bytes packet = improv::serial_packet(type, data);
    serial_->write(packet.data(), packet.size());
    serial_->flush();
}

void ImprovSerial::loop() {
    if (!serial_) {
        return;
    }
    // Bounded, so a flood of serial input cannot starve the loop.
    for (int i = 0; i < 256 && serial_->available() > 0; ++i) {
        const int byte = serial_->read();
        if (byte < 0) {
            break;
        }
        auto packet = parser_.feed(static_cast<std::uint8_t>(byte));
        if (!packet) {
            continue;
        }
        if (!*packet) {
            session_->report(packet->error());
            continue;
        }
        if ((*packet)->type != improv::PacketType::Rpc) {
            continue;
        }
        const auto rpc = improv::parse_rpc((*packet)->data, /*with_checksum=*/false);
        if (rpc) {
            session_->handle(*rpc);
        } else {
            session_->report(rpc.error());
        }
    }
    session_->loop();
}

}  // namespace tapwave::net
