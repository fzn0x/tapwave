#include "net/improv_ble.hpp"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_log.h>

namespace tapwave::net {

namespace {

constexpr const char* kTag = "improv-ble";

constexpr const char* kService = "00467768-6228-2272-4663-277478268000";
constexpr const char* kStateUuid = "00467768-6228-2272-4663-277478268001";
constexpr const char* kErrorUuid = "00467768-6228-2272-4663-277478268002";
constexpr const char* kRpcUuid = "00467768-6228-2272-4663-277478268003";
constexpr const char* kResultUuid = "00467768-6228-2272-4663-277478268004";
constexpr const char* kCapabilitiesUuid = "00467768-6228-2272-4663-277478268005";
constexpr std::uint16_t kServiceDataUuid = 0x4677;

constexpr std::uint8_t kCapabilities = improv::kCapabilityIdentify | improv::kCapabilityDeviceInfo |
                                       improv::kCapabilityWifiScan | improv::kCapabilityHostname;

class RpcCallbacks final : public BLECharacteristicCallbacks {
public:
    explicit RpcCallbacks(ImprovBle& owner) : owner_(owner) {}
    void onWrite(BLECharacteristic* characteristic) override {
        const String value = characteristic->getValue();
        owner_.on_rpc_write(reinterpret_cast<const std::uint8_t*>(value.c_str()), value.length());
    }

private:
    ImprovBle& owner_;
};

class ServerCallbacks final : public BLEServerCallbacks {
public:
    void onDisconnect(BLEServer*) override { BLEDevice::startAdvertising(); }
};

String as_string(ByteView bytes) {
    String out;
    out.reserve(bytes.size());
    for (std::uint8_t b : bytes) {
        out += static_cast<char>(b);
    }
    return out;
}

}  // namespace

bool ImprovBle::begin(const std::string& device_name, improv::DeviceHooks hooks) {
    if (running_) {
        return true;
    }
    name_ = device_name;
    BLEDevice::init(name_.c_str());
    server_ = BLEDevice::createServer();
    if (!server_) {
        ESP_LOGE(kTag, "BLE did not start");
        return false;
    }
    server_->setCallbacks(new ServerCallbacks());
    BLEService* service = server_->createService(BLEUUID(kService), 20);

    auto readable = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY;
    state_ = service->createCharacteristic(kStateUuid, readable);
    state_->addDescriptor(new BLE2902());
    error_ = service->createCharacteristic(kErrorUuid, readable);
    error_->addDescriptor(new BLE2902());
    rpc_ = service->createCharacteristic(kRpcUuid, BLECharacteristic::PROPERTY_WRITE);
    rpc_->setCallbacks(new RpcCallbacks(*this));
    result_ = service->createCharacteristic(kResultUuid, readable);
    result_->addDescriptor(new BLE2902());
    capabilities_ =
        service->createCharacteristic(kCapabilitiesUuid, BLECharacteristic::PROPERTY_READ);
    std::uint8_t capabilities = kCapabilities;
    capabilities_->setValue(&capabilities, 1);

    improv::Output output;
    output.state = [this](improv::State state) { notify_state(state); };
    output.error = [this](improv::Error error) {
        std::uint8_t value = static_cast<std::uint8_t>(error);
        error_->setValue(&value, 1);
        error_->notify();
    };
    output.result = [this](std::uint8_t command, const std::vector<std::string>& strings) {
        const Bytes frame = improv::build_rpc_result(command, strings, /*with_checksum=*/true);
        result_->setValue(const_cast<std::uint8_t*>(frame.data()), frame.size());
        result_->notify();
    };
    session_ = std::make_unique<improv::Session>(improv::Transport::Ble, std::move(hooks),
                                                 std::move(output));
    session_->begin(/*provisioned=*/false);
    std::uint8_t initial = static_cast<std::uint8_t>(session_->state());
    state_->setValue(&initial, 1);
    std::uint8_t no_error = 0;
    error_->setValue(&no_error, 1);

    service->start();
    running_ = true;
    advertise();
    ESP_LOGI(kTag, "advertising as %s", name_.c_str());
    return true;
}

void ImprovBle::advertise() {
    // Improv 2.1: the service UUID and the service data share the advertisement itself.
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->stop();
    BLEAdvertisementData data;
    data.setFlags(0x06);  // general discoverable, no classic BR/EDR
    data.setCompleteServices(BLEUUID(kService));
    data.setServiceData(BLEUUID(kServiceDataUuid), as_string(improv::advertisement_service_data(
                                                       session_->state(), kCapabilities)));
    advertising->setAdvertisementData(data);
    BLEAdvertisementData scan_response;
    scan_response.setName(name_.c_str());
    advertising->setScanResponseData(scan_response);
    advertising->start();
    advertised_ = session_->state();
}

void ImprovBle::notify_state(improv::State state) {
    std::uint8_t value = static_cast<std::uint8_t>(state);
    state_->setValue(&value, 1);
    state_->notify();
}

void ImprovBle::on_rpc_write(const std::uint8_t* data, std::size_t size) {
    std::scoped_lock lock(mutex_);
    writes_.emplace_back(data, data + size);
}

void ImprovBle::loop() {
    if (!running_) {
        return;
    }
    std::deque<Bytes> writes;
    {
        std::scoped_lock lock(mutex_);
        writes.swap(writes_);
    }
    for (const Bytes& piece : writes) {
        if (auto rpc = assembler_.feed(piece)) {
            if (*rpc) {
                session_->handle(**rpc);
            } else {
                session_->report(rpc->error());
            }
        }
    }
    session_->loop();
    if (session_->state() != advertised_) {
        advertise();  // the state is part of the advertisement
    }
}

void ImprovBle::stop() {
    if (!running_) {
        return;
    }
    BLEDevice::getAdvertising()->stop();
    // false keeps the controller memory reserved, so BLE can start again without a restart
    // (after a factory reset, or for Bluetooth audio in milestone 4).
    BLEDevice::deinit(false);
    running_ = false;
    server_ = nullptr;
    state_ = error_ = rpc_ = result_ = capabilities_ = nullptr;
    session_.reset();
    ESP_LOGI(kTag, "stopped");
}

}  // namespace tapwave::net
