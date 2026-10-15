// Turns any ISO 14443-A frontend into a TagReader: finds and reads NDEF on NFC Forum Type 2
// tags (NTAG21x, MIFARE Ultralight) and on NDEF-formatted MIFARE Classic tags.
#pragma once

#include <memory>

#include "drivers/nfc/common/readers.hpp"

namespace tapwave::nfc {

class Type2TagReader final : public TagReader {
public:
    // The largest NDEF data area read from a tag, as a guard against corrupt capability
    // containers. NTAG216 has 872 bytes; MIFARE Classic 1K has 720.
    static constexpr std::size_t kMaxDataArea = 2048;

    explicit Type2TagReader(std::unique_ptr<Iso14443aReader> frontend);

    const ReaderInfo& info() const override { return frontend_->info(); }
    std::expected<std::optional<Uid>, Error> detect() override;
    std::expected<TagRead, Error> read() override;
    std::expected<void, Error> write_ndef(ByteView message) override;

    Iso14443aReader& frontend() { return *frontend_; }

private:
    void read_type2(TagRead& out);
    void read_classic(TagRead& out);
    TagFamily identify_type2(const TagRead& out);

    std::unique_ptr<Iso14443aReader> frontend_;
};

}  // namespace tapwave::nfc
