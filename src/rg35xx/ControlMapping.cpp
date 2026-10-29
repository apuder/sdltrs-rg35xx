#include "ControlMapping.hpp"

#include <utility>

namespace rg35xx {
namespace {

// Preserve empty fields so malformed separators cannot be silently ignored.
std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return parts;
        start = end + 1;
    }
}

std::uint32_t controlMask(const std::string& token)
{
    static const char* buttons[] = {
        "A", "B", "X", "Y", "D_U", "D_D", "D_L", "D_R",
        "START", "SELECT", "", "L1", "R1", "L2", "R2"
    };
    for (std::size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        if (!token.empty() && token == buttons[i])
            return ControlMapping::buttonMask(static_cast<Button>(i));
    static const char* axes[] = {
        "LS_L", "LS_R", "LS_U", "LS_D", "RS_L", "RS_R", "RS_U", "RS_D"
    };
    for (std::size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i)
        if (token == axes[i])
            return ControlMapping::axisMask(static_cast<Axis>(i / 2), i % 2 ? 1 : -1);
    return 0;
}

bool parseControls(const std::string& text, std::uint32_t& mask)
{
    mask = 0;
    const std::vector<std::string> tokens = split(text, '+');
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::uint32_t bit = controlMask(tokens[i]);
        if (!bit || (mask & bit)) return false;
        mask |= bit;
    }
    return true;
}

int keyBit(const std::string& token)
{
    if (token.size() == 1) {
        const char c = token[0];
        if (c == '@') return 0;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        if (c >= '0' && c <= '9') return c - '0' + 32;
    }
    static const std::pair<const char*, int> keys[] = {
        {"SEMICOLON", 43}, {"COMMA", 44}, {"PERIOD", 46},
        {"ENTER", 48}, {"CLEAR", 49}, {"BREAK", 50},
        {"UP", 51}, {"DOWN", 52}, {"LEFT", 53}, {"RIGHT", 54},
        {"SPACE", 55}, {"SHIFT", 56}
    };
    for (std::size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (token == keys[i].first) return keys[i].second;
    return -1;
}

bool parseKeys(const std::string& text, std::uint64_t& keys)
{
    // Accumulate the complete physical-key chord; there is no fixed key-count limit.
    keys = 0;
    const std::vector<std::string> tokens = split(text, '+');
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const int bit = keyBit(tokens[i]);
        if (bit < 0) return false;
        const std::uint64_t key = UINT64_C(1) << bit;
        if (keys & key) return false;
        keys |= key;
    }
    return true;
}

} // namespace

std::uint32_t ControlMapping::buttonMask(Button button)
{
    // Menu and stick clicks are reserved or have no mapping token.
    if (button == Button::Menu || button >= Button::LeftStick) return 0;
    return UINT32_C(1) << static_cast<unsigned int>(button);
}

std::uint32_t ControlMapping::axisMask(Axis axis, int direction)
{
    if (direction == 0) return 0;
    return UINT32_C(1) << (16 + static_cast<unsigned int>(axis) * 2 + (direction > 0 ? 1 : 0));
}

bool ControlMapping::parse(const std::string& text, ControlMapping& result,
                        std::string& error)
{
    error.clear();
    if (text.size() > kMaxLength) {
        error = "mapping exceeds 4095 bytes";
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (static_cast<unsigned char>(text[i]) < 33 ||
            static_cast<unsigned char>(text[i]) > 126) {
            error = "mapping must contain printable ASCII without spaces";
            return false;
        }
    }

    ControlMapping parsed;
    const std::vector<std::string> records = split(text, ',');
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::vector<std::string> fields = split(records[i], ':');
        if (fields.size() != 2) {
            error = "expected CONTROL:KEY pairs";
            return false;
        }
        Binding binding;
        if (!parseControls(fields[0], binding.controls)) {
            error = "unknown, empty, or repeated control";
            return false;
        }
        if (!parseKeys(fields[1], binding.keys)) {
            error = "unknown, empty, or repeated TRS-80 key";
            return false;
        }
        for (std::size_t j = 0; j < parsed.bindings_.size(); ++j) {
            if (parsed.bindings_[j].controls == binding.controls) {
                error = "repeated control/chord; combine output keys with +";
                return false;
            }
        }
        parsed.bindings_.push_back(binding);
    }
    result = parsed;
    return true;
}

std::uint64_t ControlMapping::resolve(std::uint32_t controls) const
{
    std::uint64_t keys = 0;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const Binding& binding = bindings_[i];
        if ((controls & binding.controls) != binding.controls)
            continue;
        bool replaced = false;
        for (std::size_t j = 0; j < bindings_.size(); ++j) {
            const Binding& other = bindings_[j];
            if (other.controls != binding.controls &&
                (other.controls & binding.controls) == binding.controls &&
                (controls & other.controls) == other.controls) {
                replaced = true;
                break;
            }
        }
        if (!replaced) keys |= binding.keys;
    }
    return keys;
}

std::uint32_t ControlMapping::overriddenControls(std::uint32_t controls) const
{
    std::uint32_t overridden = 0;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        if ((controls & bindings_[i].controls) == bindings_[i].controls)
            overridden |= bindings_[i].controls;
    }
    return overridden;
}

bool ControlMapping::hasBindingWithControl(std::uint32_t control_mask) const
{
    if (control_mask == 0) return false;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        if (bindings_[i].controls & control_mask)
            return true;
    }
    return false;
}

} // namespace rg35xx
