#include "drivers/nfc/common/nfc_types.hpp"

namespace tapwave::nfc {

std::string_view describe(Error error) {
    switch (error) {
        case Error::NoTag:
            return "no tag in the field";
        case Error::Timeout:
            return "the reader did not answer in time";
        case Error::Transport:
            return "the reader stopped answering on its bus";
        case Error::Protocol:
            return "the reader sent an unexpected answer";
        case Error::Crc:
            return "a corrupted frame was received";
        case Error::AuthFailed:
            return "the tag refused authentication";
        case Error::NotSupported:
            return "not supported by this tag or reader";
        case Error::Collision:
            return "more than one tag is on the reader";
        case Error::ReadOnly:
            return "the tag is locked";
        case Error::TooLarge:
            return "the data does not fit on the tag";
    }
    return "unknown NFC error";
}

std::string ReaderInfo::summary() const {
    std::string out = model;
    if (!firmware.empty()) {
        out += ' ';
        out += firmware;
    }
    if (!bus.empty()) {
        out += " (";
        out += bus;
        out += ')';
    }
    return out;
}

std::string_view to_string(TagFamily family) {
    switch (family) {
        case TagFamily::Unknown:
            return "Unknown";
        case TagFamily::Type2:
            return "Type 2 tag";
        case TagFamily::Ntag210:
            return "NTAG210";
        case TagFamily::Ntag212:
            return "NTAG212";
        case TagFamily::Ntag213:
            return "NTAG213";
        case TagFamily::Ntag215:
            return "NTAG215";
        case TagFamily::Ntag216:
            return "NTAG216";
        case TagFamily::Ultralight:
            return "MIFARE Ultralight";
        case TagFamily::UltralightEv1:
            return "MIFARE Ultralight EV1";
        case TagFamily::MifareClassicMini:
            return "MIFARE Classic Mini";
        case TagFamily::MifareClassic1k:
            return "MIFARE Classic 1K";
        case TagFamily::MifareClassic4k:
            return "MIFARE Classic 4K";
        case TagFamily::Iso14443_4:
            return "ISO 14443-4";
    }
    return "Unknown";
}

bool is_type2(TagFamily family) {
    switch (family) {
        case TagFamily::Type2:
        case TagFamily::Ntag210:
        case TagFamily::Ntag212:
        case TagFamily::Ntag213:
        case TagFamily::Ntag215:
        case TagFamily::Ntag216:
        case TagFamily::Ultralight:
        case TagFamily::UltralightEv1:
            return true;
        default:
            return false;
    }
}

bool is_classic(TagFamily family) {
    return family == TagFamily::MifareClassicMini || family == TagFamily::MifareClassic1k ||
           family == TagFamily::MifareClassic4k;
}

}  // namespace tapwave::nfc
