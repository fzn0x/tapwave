#include "doctest_main.hpp"

#include <numeric>

#include "core/improv.hpp"

using namespace tapwave;
using namespace tapwave::improv;

namespace {

std::uint8_t sum(ByteView bytes) {
    return static_cast<std::uint8_t>(std::accumulate(bytes.begin(), bytes.end(), 0u));
}

// The example from the Improv spec: SSID "MyWirelessAP", password "mysecurepassword".
Bytes wifi_settings_frame(bool with_checksum) {
    Bytes frame{0x01, 0x1E, 0x0C};
    const std::string_view ssid = "MyWirelessAP";
    const std::string_view password = "mysecurepassword";
    frame.insert(frame.end(), ssid.begin(), ssid.end());
    frame.push_back(0x10);
    frame.insert(frame.end(), password.begin(), password.end());
    if (with_checksum) {
        frame.push_back(sum(frame));
    }
    return frame;
}

std::vector<SerialParser::Packet> feed_all(SerialParser& parser, ByteView bytes,
                                           std::vector<Error>* errors = nullptr) {
    std::vector<SerialParser::Packet> packets;
    for (std::uint8_t b : bytes) {
        if (auto result = parser.feed(b)) {
            if (*result) {
                packets.push_back(**result);
            } else if (errors) {
                errors->push_back(result->error());
            }
        }
    }
    return packets;
}

}  // namespace

TEST_CASE("parses the spec's Wi-Fi settings example") {
    const auto rpc = parse_rpc(wifi_settings_frame(false), false);
    REQUIRE(rpc);
    CHECK(rpc->command == 0x01);
    REQUIRE(rpc->args.size() == 2);
    CHECK(rpc->args[0] == "MyWirelessAP");
    CHECK(rpc->args[1] == "mysecurepassword");
}

TEST_CASE("BLE frames carry and check a checksum") {
    Bytes frame = wifi_settings_frame(true);
    CHECK(parse_rpc(frame, true));
    frame.back() ^= 0x01;
    CHECK(parse_rpc(frame, true).error() == Error::InvalidRpc);
}

TEST_CASE("open networks send an empty password") {
    const Bytes frame{0x01, 0x06, 0x04, 'H', 'o', 'm', 'e', 0x00};
    const auto rpc = parse_rpc(frame, false);
    REQUIRE(rpc);
    CHECK(rpc->args[1].empty());
}

TEST_CASE("commands without data") {
    for (std::uint8_t command : {0x02, 0x03, 0x04, 0x05, 0x06, 0x07}) {
        const auto rpc = parse_rpc(Bytes{command, 0x00}, false);
        REQUIRE(rpc);
        CHECK(rpc->args.empty());
    }
}

TEST_CASE("setting the hostname sends one string") {
    const Bytes frame{0x05, 0x05, 0x04, 'b', 'o', 'x', '1'};
    const auto rpc = parse_rpc(frame, false);
    REQUIRE(rpc);
    CHECK(rpc->args == std::vector<std::string>{"box1"});
}

TEST_CASE("malformed RPCs map to Improv errors") {
    CHECK(parse_rpc(Bytes{0x01}, false).error() == Error::InvalidRpc);
    CHECK(parse_rpc(Bytes{0x01, 0x05, 0x01}, false).error() == Error::InvalidRpc);        // length
    CHECK(parse_rpc(Bytes{0x01, 0x02, 0x05, 'a'}, false).error() == Error::InvalidRpc);   // string
    CHECK(parse_rpc(Bytes{0x01, 0x02, 0x00, 0x00}, false).error() == Error::InvalidRpc);  // no ssid
    CHECK(parse_rpc(Bytes{0x01, 0x02, 0x01, 'a'}, false).error() == Error::InvalidRpc);   // one arg
    CHECK(parse_rpc(Bytes{0x05, 0x04, 0x01, 'a', 0x01, 'b'}, false).error() == Error::InvalidRpc);
    CHECK(parse_rpc(Bytes{0x42, 0x00}, false).error() == Error::UnknownRpc);
}

TEST_CASE("RPC results are length-prefixed strings") {
    const std::vector<std::string> info{"Tapwave", "0.1.0", "ESP32", "Kids box"};
    const Bytes result = build_rpc_result(0x03, info, false);
    CHECK(result[0] == 0x03);
    CHECK(result[1] == result.size() - 2);
    CHECK(result[2] == 7);
    CHECK(to_string(ByteView(result).subspan(3, 7)) == "Tapwave");
    // Results parse back with the same string list encoding.
    const auto back = parse_rpc(result, false);
    REQUIRE(back);
    CHECK(back->args == info);

    const Bytes with_checksum = build_rpc_result(0x01, std::vector<std::string>{"http://x/"}, true);
    CHECK(with_checksum.back() == sum(ByteView(with_checksum).first(with_checksum.size() - 1)));

    const Bytes empty = build_rpc_result(0x04, std::vector<std::string>{}, false);
    CHECK(empty == Bytes{0x04, 0x00});
}

TEST_CASE("serial packets match the spec layout") {
    const Bytes packet = serial_packet(PacketType::CurrentState, Bytes{0x02});
    REQUIRE(packet.size() == 12);
    CHECK(to_string(ByteView(packet).first(6)) == "IMPROV");
    CHECK(packet[6] == 0x01);
    CHECK(packet[7] == 0x01);
    CHECK(packet[8] == 0x01);
    CHECK(packet[9] == 0x02);
    CHECK(packet[10] == sum(ByteView(packet).first(10)));
    CHECK(packet[11] == '\n');
}

TEST_CASE("the serial parser finds packets between log lines") {
    Bytes stream;
    const std::string_view log = "I (123) wifi: started\nIMPRO";
    stream.insert(stream.end(), log.begin(), log.end());
    Bytes rpc = serial_packet(PacketType::Rpc, wifi_settings_frame(false));
    rpc.pop_back();  // no newline from the client
    stream.insert(stream.end(), rpc.begin(), rpc.end());
    stream.insert(stream.end(), {'o', 'k', '\n'});

    SerialParser parser;
    const auto packets = feed_all(parser, stream);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].type == PacketType::Rpc);
    const auto parsed = parse_rpc(packets[0].data, false);
    REQUIRE(parsed);
    CHECK(parsed->args[0] == "MyWirelessAP");
}

TEST_CASE("the serial parser reports bad checksums and recovers") {
    Bytes bad = serial_packet(PacketType::Rpc, Bytes{0x03, 0x00});
    bad[bad.size() - 2] ^= 0xFF;
    Bytes good = serial_packet(PacketType::Rpc, Bytes{0x02, 0x00});

    SerialParser parser;
    std::vector<Error> errors;
    auto packets = feed_all(parser, bad, &errors);
    CHECK(packets.empty());
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == Error::InvalidRpc);
    packets = feed_all(parser, good);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].data == Bytes{0x02, 0x00});
}

TEST_CASE("the serial parser ignores other protocol versions") {
    Bytes packet = serial_packet(PacketType::Rpc, Bytes{0x02, 0x00});
    packet[6] = 0x02;
    SerialParser parser;
    CHECK(feed_all(parser, packet).empty());
}

TEST_CASE("BLE writes are reassembled across pieces") {
    const Bytes frame = wifi_settings_frame(true);
    BleRpcAssembler assembler;
    CHECK_FALSE(assembler.feed(ByteView(frame).first(20)));
    const auto done = assembler.feed(ByteView(frame).subspan(20));
    REQUIRE(done);
    REQUIRE(*done);
    CHECK((*done)->args[1] == "mysecurepassword");

    // The assembler starts fresh after a frame.
    const Bytes identify{0x02, 0x00, 0x02};
    const auto next = assembler.feed(identify);
    REQUIRE(next);
    CHECK((*next)->command == 0x02);
}

TEST_CASE("advertisement service data") {
    CHECK(advertisement_service_data(State::Authorized, 0x0F) ==
          Bytes{0x02, 0x0F, 0x00, 0x00, 0x00, 0x00});
}
