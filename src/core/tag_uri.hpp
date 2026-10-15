// The tapwave:// URI scheme (ARCHITECTURE.md section 9).
//
//   tapwave://<action>/<target>[?<key>=<value>&...]
//
// The target is percent-decoded once after the options are split off. Unknown actions parse
// fine and report supported() == false, so old firmware ignores new kinds of cards.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tapwave::uri {

enum class Action : std::uint8_t { Sd, Stream, Ha, Cmd, Unknown };

std::string_view to_string(Action action);

struct Option {
    std::string key;
    std::string value;

    friend bool operator==(const Option&, const Option&) = default;
};

struct TapwaveUri {
    Action action = Action::Unknown;
    std::string action_name;  // lowercase, as written on the card
    std::string target;       // percent-decoded
    std::vector<Option> options;

    bool supported() const { return action != Action::Unknown; }

    // The first option with this key.
    std::optional<std::string_view> option(std::string_view key) const;

    friend bool operator==(const TapwaveUri&, const TapwaveUri&) = default;
};

enum class Error : std::uint8_t {
    NotTapwave,     // the scheme is not tapwave:
    MissingAction,  // nothing between "tapwave://" and the next "/"
    BadAction,      // the action has characters other than letters, digits and "-"
    BadEncoding,    // a "%" not followed by two hex digits
};

std::string_view describe(Error error);

// True if the URI uses the tapwave: scheme (case-insensitive).
bool has_tapwave_scheme(std::string_view uri);

std::expected<TapwaveUri, Error> parse(std::string_view uri);

// Writes the canonical form: lowercase action, the target with "%", "?", "#", spaces and
// non-printable or non-ASCII bytes percent-encoded, options encoded the same way plus "&" and "=".
std::string format(const TapwaveUri& uri);

// Decodes %XX escapes. Returns nullopt for a malformed escape.
std::optional<std::string> percent_decode(std::string_view text);

}  // namespace tapwave::uri
