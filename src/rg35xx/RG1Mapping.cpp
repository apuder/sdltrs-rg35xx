#include "RG1Mapping.hpp"

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
            return RG1Mapping::buttonMask(static_cast<Button>(i));
    static const char* axes[] = {
        "LS_L", "LS_R", "LS_U", "LS_D", "RS_L", "RS_R", "RS_U", "RS_D"
    };
    for (std::size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i)
        if (token == axes[i])
            return RG1Mapping::axisMask(static_cast<Axis>(i / 2), i % 2 ? 1 : -1);
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

bool parseAction(const std::string& text, RG1Action& action)
{
    action.keys = 0;
    if (text == "ANY_KEY") { action.kind = RG1Action::AnyKey; return true; }
    if (text == "TEXT_INPUT") { action.kind = RG1Action::TextInput; return true; }
    if (text == "NUMERIC_INPUT") { action.kind = RG1Action::NumericInput; return true; }
    action.kind = RG1Action::Keys;
    const std::vector<std::string> tokens = split(text, '+');
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const int bit = keyBit(tokens[i]);
        if (bit < 0) return false;
        const std::uint64_t key = UINT64_C(1) << bit;
        if (action.keys & key) return false;
        action.keys |= key;
    }
    return true;
}

} // namespace

std::uint32_t RG1Mapping::buttonMask(Button button)
{
    // Menu and stick clicks are reserved or have no RG1 token.
    if (button == Button::Menu || button >= Button::LeftStick) return 0;
    return UINT32_C(1) << static_cast<unsigned int>(button);
}

std::uint32_t RG1Mapping::axisMask(Axis axis, int direction)
{
    if (direction == 0) return 0;
    return UINT32_C(1) << (16 + static_cast<unsigned int>(axis) * 2 + (direction > 0 ? 1 : 0));
}

bool RG1Mapping::parse(const std::string& text, RG1Mapping& result,
                        std::string& error)
{
    error.clear();
    if (text.size() > kMaxLength || text.compare(0, 4, "RG1;") != 0) {
        error = "expected RG1; and at most 4095 bytes";
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (static_cast<unsigned char>(text[i]) < 33 ||
            static_cast<unsigned char>(text[i]) > 126) {
            error = "mapping must contain printable ASCII without spaces";
            return false;
        }
    }

    RG1Mapping parsed;
    const std::vector<std::string> records = split(text.substr(4), ';');
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::vector<std::string> fields = split(records[i], '=');
        if (fields.size() != 2) {
            error = "expected trigger=action";
            return false;
        }
        RG1Binding binding;
        binding.kind = RG1Binding::Physical;
        std::string trigger = fields[0];
        if (trigger.compare(0, 7, "PROMPT(") == 0 && trigger.back() == ')') {
            binding.kind = RG1Binding::Prompt;
            trigger = trigger.substr(7, trigger.size() - 8);
        } else if (trigger.compare(0, 11, "OSK(SELECT,") == 0 && trigger.back() == ')') {
            RG1Action overlay;
            if (!parseAction(trigger.substr(11, trigger.size() - 12), overlay)) {
                error = "invalid overlay action";
                return false;
            }
            binding.kind = RG1Binding::Overlay;
            trigger = "SELECT";
        }
        if (!parseControls(trigger, binding.controls)) {
            error = "unknown, empty, or repeated control";
            return false;
        }
        const std::vector<std::string> actions = split(fields[1], '|');
        for (std::size_t j = 0; j < actions.size(); ++j) {
            RG1Action action;
            if (!parseAction(actions[j], action)) {
                error = "unknown, empty, or repeated TRS-80 key";
                return false;
            }
            binding.actions.push_back(action);
        }
        parsed.bindings_.push_back(binding);
    }
    result = parsed;
    return true;
}

std::uint64_t RG1Mapping::resolve(std::uint32_t controls) const
{
    std::uint64_t keys = 0;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const RG1Binding& binding = bindings_[i];
        if (binding.kind != RG1Binding::Physical ||
            binding.actions.front().kind != RG1Action::Keys ||
            (controls & binding.controls) != binding.controls)
            continue;
        bool replaced = false;
        for (std::size_t j = 0; j < bindings_.size(); ++j) {
            const RG1Binding& other = bindings_[j];
            if (other.kind == RG1Binding::Physical &&
                other.actions.front().kind == RG1Action::Keys &&
                other.controls != binding.controls &&
                (other.controls & binding.controls) == binding.controls &&
                (controls & other.controls) == other.controls) {
                replaced = true;
                break;
            }
        }
        if (!replaced) keys |= binding.actions.front().keys;
    }
    return keys;
}

} // namespace rg35xx
