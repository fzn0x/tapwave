#include "core/rtttl.hpp"

#include <cmath>
#include <optional>

namespace tapwave::rtttl {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<int> number(std::string_view s) {
    if (s.empty() || s.size() > 4) {
        return std::nullopt;
    }
    int value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

bool valid_duration(int d) {
    return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32;
}

// Semitones above C for c d e f g a b (h is the German name for b).
std::optional<int> semitone(char note) {
    switch (note) {
        case 'c':
            return 0;
        case 'd':
            return 2;
        case 'e':
            return 4;
        case 'f':
            return 5;
        case 'g':
            return 7;
        case 'a':
            return 9;
        case 'b':
        case 'h':
            return 11;
        default:
            return std::nullopt;
    }
}

std::uint16_t frequency(int semitone_from_c, int octave) {
    // A4 = 440 Hz; semitone 9 of octave 4.
    const double steps = (octave - 4) * 12 + (semitone_from_c - 9);
    return static_cast<std::uint16_t>(std::lround(440.0 * std::pow(2.0, steps / 12.0)));
}

}  // namespace

std::expected<std::vector<Tone>, Error> parse(std::string_view melody) {
    const std::size_t first = melody.find(':');
    const std::size_t second =
        first == std::string_view::npos ? first : melody.find(':', first + 1);
    if (second == std::string_view::npos) {
        return std::unexpected(Error::Format);
    }

    int duration = 4;
    int octave = 6;
    int bpm = 63;
    std::string_view defaults = melody.substr(first + 1, second - first - 1);
    while (!defaults.empty()) {
        const std::size_t comma = defaults.find(',');
        const std::string_view item = trim(defaults.substr(0, comma));
        defaults =
            comma == std::string_view::npos ? std::string_view{} : defaults.substr(comma + 1);
        if (item.empty()) {
            continue;
        }
        if (item.size() < 3 || item[1] != '=') {
            return std::unexpected(Error::Format);
        }
        const auto value = number(item.substr(2));
        if (!value) {
            return std::unexpected(Error::Format);
        }
        switch (item[0] | 0x20) {
            case 'd':
                if (!valid_duration(*value)) {
                    return std::unexpected(Error::Format);
                }
                duration = *value;
                break;
            case 'o':
                if (*value < 3 || *value > 8) {
                    return std::unexpected(Error::Format);
                }
                octave = *value;
                break;
            case 'b':
                if (*value < 10 || *value > 900) {
                    return std::unexpected(Error::Format);
                }
                bpm = *value;
                break;
            default:
                return std::unexpected(Error::Format);
        }
    }

    const double whole_note_ms = 60000.0 * 4 / bpm;
    std::vector<Tone> tones;
    std::string_view notes = melody.substr(second + 1);
    while (!notes.empty()) {
        const std::size_t comma = notes.find(',');
        std::string_view item = trim(notes.substr(0, comma));
        notes = comma == std::string_view::npos ? std::string_view{} : notes.substr(comma + 1);
        if (item.empty()) {
            continue;
        }

        std::size_t pos = 0;
        int note_duration = duration;
        while (pos < item.size() && item[pos] >= '0' && item[pos] <= '9') {
            ++pos;
        }
        if (pos > 0) {
            const auto d = number(item.substr(0, pos));
            if (!d || !valid_duration(*d)) {
                return std::unexpected(Error::Note);
            }
            note_duration = *d;
        }
        if (pos >= item.size()) {
            return std::unexpected(Error::Note);
        }
        const char name = static_cast<char>(item[pos] | 0x20);
        ++pos;
        const bool rest = name == 'p';
        std::optional<int> tone;
        if (!rest) {
            tone = semitone(name);
            if (!tone) {
                return std::unexpected(Error::Note);
            }
        }
        if (pos < item.size() && item[pos] == '#') {
            if (rest) {
                return std::unexpected(Error::Note);
            }
            ++*tone;
            ++pos;
        }
        bool dotted = false;
        if (pos < item.size() && item[pos] == '.') {
            dotted = true;
            ++pos;
        }
        int note_octave = octave;
        if (pos < item.size() && item[pos] >= '3' && item[pos] <= '8') {
            note_octave = item[pos] - '0';
            ++pos;
        }
        if (pos < item.size() && item[pos] == '.') {  // some writers put the dot last
            dotted = true;
            ++pos;
        }
        if (pos != item.size()) {
            return std::unexpected(Error::Note);
        }

        double ms = whole_note_ms / note_duration;
        if (dotted) {
            ms *= 1.5;
        }
        if (tones.size() == kMaxNotes) {
            return std::unexpected(Error::TooLong);
        }
        tones.push_back({rest ? std::uint16_t{0} : frequency(*tone, note_octave),
                         static_cast<std::uint16_t>(std::lround(ms))});
    }
    if (tones.empty()) {
        return std::unexpected(Error::Format);
    }
    return tones;
}

}  // namespace tapwave::rtttl
