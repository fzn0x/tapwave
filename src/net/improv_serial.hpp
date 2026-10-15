// Improv Wi-Fi over the USB serial port, always on. ESP Web Tools and the Improv website use
// it to send Wi-Fi details right after flashing.
#pragma once

#include <Stream.h>

#include <memory>

#include "core/improv.hpp"
#include "core/improv_session.hpp"

namespace tapwave::net {

class ImprovSerial {
public:
    void begin(Stream& serial, improv::DeviceHooks hooks, bool provisioned);
    void loop();

private:
    void send(improv::PacketType type, ByteView data);

    Stream* serial_ = nullptr;
    improv::SerialParser parser_;
    std::unique_ptr<improv::Session> session_;
};

}  // namespace tapwave::net
