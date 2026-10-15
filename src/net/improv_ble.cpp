#include "net/improv_ble.hpp"

#include <NimBLEDevice.h>
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

class RpcCallbacks final : public NimBLECharacteristicCallbacks {
public:
    explicit RpcCallbacks(ImprovBle& owner) : owner_(owner) {}
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        const NimBLEAttValue value = characteristic->getValue();
        owner_.on_rpc_write(value.data(), value.size());
    }

private:
    ImprovBle& owner_;
};

class ServerCallbacks final : public NimBLEServerCallbacks {
public:
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        NimBLEDevice::getAdvertising()->start();  // stay discoverable for the next try
    }
};

ServerCallbacks server_callbacks;

}  // namespace

bool ImprovBle::begin(const std::string& device_name, improv::DeviceHooks hooks) {
    if (running_) {
        return true;
    }
    name_ = device_name;
    if (!NimBLEDevice::init(name_)) {
        ESP_LOGE(kTag, "BLE did not start");
        return false;
    }
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&server_callbacks, /*deleteCallbacks=*/false);
    NimBLEService* service = server->createService(kService);

    const std::uint32_t readable = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY;
    state_ = service->createCharacteristic(kStateUuid, readable);
    error_ = service->createCharacteristic(kErrorUuid, readable);
    NimBLECharacteristic* rpc = service->createCharacteristic(kRpcUuid, NIMBLE_PROPERTY::WRITE);
    rpc->setCallbacks(new RpcCallbacks(*this));  // owned and freed by NimBLE
    result_ = service->createCharacteristic(kResultUuid, readable);
    NimBLECharacteristic* capabilities =
        service->createCharacteristic(kCapabilitiesUuid, NIMBLE_PROPERTY::READ);
    const std::uint8_t caps = kCapabilities;
    capabilities->setValue(&caps, 1);

    improv::Output output;
    output.state = [this](improv::State state) {
        const auto value = static_cast<std::uint8_t>(state);
        state_->setValue(&value, 1);
        state_->notify();
    };
    output.error = [this](improv::Error error) {
        const auto value = static_cast<std::uint8_t>(error);
        error_->setValue(&value, 1);
        error_->notify();
    };
    output.result = [this](std::uint8_t command, const std::vector<std::string>& strings) {
        const Bytes frame = improv::build_rpc_result(command, strings, /*with_checksum=*/true);
        result_->setValue(frame.data(), frame.size());
        result_->notify();
    };
    session_ = std::make_unique<improv::Session>(improv::Transport::Ble, std::move(hooks),
                                                 std::move(output));
    session_->begin(/*provisioned=*/false);
    const auto initial = static_cast<std::uint8_t>(session_->state());
    state_->setValue(&initial, 1);
    const std::uint8_t no_error = 0;
    error_->setValue(&no_error, 1);

    server->start();
    running_ = true;
    advertise();
    ESP_LOGI(kTag, "advertising as %s", name_.c_str());
    return true;
}

void ImprovBle::advertise() {
    // Improv 2.1: the service UUID and the service data share the advertisement itself.
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setCompleteServices(NimBLEUUID(kService));
    data.setServiceData(NimBLEUUID(kServiceDataUuid),
                        improv::advertisement_service_data(session_->state(), kCapabilities));
    advertising->setAdvertisementData(data);
    NimBLEAdvertisementData scan_response;
    scan_response.setName(name_);
    advertising->setScanResponseData(scan_response);
    advertising->start();
    advertised_ = session_->state();
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
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(/*clearAll=*/true);
    running_ = false;
    state_ = error_ = result_ = nullptr;
    session_.reset();
    ESP_LOGI(kTag, "stopped");
}

}  // namespace tapwave::net
